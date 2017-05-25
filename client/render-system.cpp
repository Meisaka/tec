﻿// Copyright (c) 2013-2016 Trillek contributors. See AUTHORS.txt for details
// Licensed under the terms of the LGPLv3. See licenses/lgpl-3.0.txt

#include "render-system.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <spdlog/spdlog.h>
#include <thread>
#include <cmath>

#include "graphics/shader.hpp"
#include "graphics/animation.hpp"
#include "graphics/view.hpp"
#include "graphics/material.hpp"
#include "graphics/lights.hpp"
#include "graphics/renderable.hpp"
#include "graphics/texture-object.hpp"
#include "resources/pixel-buffer.hpp"
#include "components/transforms.hpp"
#include "resources/mesh.hpp"
#include "resources/obj.hpp"
#include "entity.hpp"
#include "events.hpp"
#include "multiton.hpp"

namespace tec {
	typedef Multiton<eid, PointLight*> PointLightMap;
	typedef Multiton<eid, DirectionalLight*> DirectionalLightMap;

	RenderSystem::RenderSystem() : current_view(nullptr),
		window_width(1024), window_height(768) {
		_log = spdlog::get("console_log");

		GLenum err = glGetError();
		// If there is an error that means something went wrong when creating the context.
		if (err) {
			_log->debug("[RenderSystem] Something went wrong when creating the context.");
			return;
		}

		// Black is the safest clear color since this is a space game.
		glClearColor(0.0, 0.0, 0.0, 0.0);
		std::shared_ptr<OBJ> sphere = OBJ::Create(FilePath::GetAssetPath("/sphere/sphere.obj"));
		if (!sphere) {
			_log->debug("[RenderSystem] Error loading sphere.obj.");
			this->sphere_vbo.Load(std::vector<VertexData>(), std::vector<GLuint>());
		}
		else {
			this->sphere_vbo.Load(sphere);
		}
		std::shared_ptr<OBJ> quad = OBJ::Create(FilePath::GetAssetPath("/quad/quad.obj"));
		if (!quad) {
			_log->debug("[RenderSystem] Error loading quad.obj.");
			this->quad_vbo.Load(std::vector<VertexData>(), std::vector<GLuint>());
		}
		else {
			this->quad_vbo.Load(quad);
		}

		this->light_gbuffer.AddColorAttachments(4, this->window_width, this->window_height);
		this->light_gbuffer.SetDepthAttachment(GBuffer::GBUFFER_DEPTH_TYPE_STENCIL,
			this->window_width, this->window_height);
		if (!this->light_gbuffer.CheckCompletion()) {
			_log->error() << "[RenderSystem] Failed to create Light GBuffer.";
		}

		this->shadow_gbuffer.SetDepthAttachment(GBuffer::GBUFFER_DEPTH_TYPE_SHADOW, 4096, 4096);
		if (!this->shadow_gbuffer.CheckCompletion()) {
			_log->error() << "[RenderSystem] Failed to create Shadow GBuffer.";
		}
		std::uint8_t tmp_buff[] = {
#include "resources/checker.c" // Carmack's trick . Contains a 128x128x1 bytes of monocrome texture data
		};
		std::shared_ptr<PixelBuffer> default_pbuffer = std::make_shared<PixelBuffer>(64, 64, 8, ImageColorMode::COLOR_RGBA);
		std::copy_n(tmp_buff, sizeof(tmp_buff) - 1, default_pbuffer->LockWrite());
		default_pbuffer->UnlockWrite();
		PixelBufferMap::Set("default", default_pbuffer);

		std::shared_ptr<TextureObject> default_texture = std::make_shared<TextureObject>(default_pbuffer);
		TextureMap::Set("default", default_texture);
	}

	void RenderSystem::SetViewportSize(const unsigned int width, const unsigned int height) {
		this->window_height = height;
		this->window_width = width;


		float aspect_ratio = static_cast<float>(this->window_width) / static_cast<float>(this->window_height);
		if ((aspect_ratio < 1.0f) || std::isnan(aspect_ratio)) {
			aspect_ratio = 4.0f / 3.0f;
		}

		this->projection = glm::perspective(
			glm::radians(45.0f),
			aspect_ratio,
			0.1f,
			10000.0f
		);
		this->light_gbuffer.ResizeColorAttachments(this->window_width, this->window_height);
		this->light_gbuffer.ResizeDepthAttachment(this->window_width, this->window_height);
	}

	void RenderSystem::Update(const double delta, const GameState& state) {
		ProcessCommandQueue();
		EventQueue<WindowResizedEvent>::ProcessEventQueue();
		EventQueue<EntityCreated>::ProcessEventQueue();
		EventQueue<EntityDestroyed>::ProcessEventQueue();

		UpdateRenderList(delta, state);
		this->light_gbuffer.StartFrame();

		// Set the common states here that hold don't change often.
		glDepthMask(GL_TRUE);
		glEnable(GL_DEPTH_TEST);
		glDepthFunc(GL_LESS);
		glEnable(GL_CULL_FACE);
		glCullFace(GL_FRONT);
		glDisable(GL_BLEND);

		ShadowPass();
		// The shadow pass changes the viewport.
		glViewport(0, 0, this->window_width, this->window_height);

		GeometryPass();

		// More common state changes.
		glDepthMask(GL_FALSE);
		glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

		glEnable(GL_STENCIL_TEST);
		PointLightPass();
		glDisable(GL_STENCIL_TEST);
		DirectionalLightPass();
		FinalPass();
		//RenderGbuffer();
	}

	void RenderSystem::ShadowPass() {
		this->shadow_gbuffer.ShadowPass();

		std::shared_ptr<Shader> def_shader = ShaderMap::Get("deferred_shadow");
		def_shader->Use();

		glm::mat4 camera_matrix(1.0);
		{
			View* view = this->current_view;
			if (view) {
				camera_matrix = view->view_matrix;
			}
		}

		GLint animatrix_loc = def_shader->GetUniformLocation("animation_matrix");
		GLint animated_loc = def_shader->GetUniformLocation("animated");
		GLint model_index = def_shader->GetUniformLocation("model");
		GLint depthMVP_index = def_shader->GetUniformLocation("depthMVP");

		glm::mat4 depthProjectionMatrix = glm::ortho(-10.0, 10.0, -10.0, 10.0, -100.0, 100.0);
		glm::mat4 depthModelMatrix = glm::mat4(1.0);

		for (auto itr = DirectionalLightMap::Begin(); itr != DirectionalLightMap::End(); ++itr) {
			DirectionalLight* light = itr->second;
			glm::vec3 lightInvDir = light->direction * -1.0f;

			// Compute the MVP matrix from the light's point of view
			glm::mat4 depthViewMatrix = glm::lookAt(lightInvDir, glm::vec3(0, 0, 0), glm::vec3(0.0, 1.0, 0.0));
			glm::mat4 depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;

			// Send our transformation to the currently bound shader,
			// in the "MVP" uniform
			glUniformMatrix4fv(depthMVP_index, 1, GL_FALSE, glm::value_ptr(depthMVP));
			for (auto shader_list : this->render_item_list) {
				for (auto render_item : shader_list.second) {
					glBindVertexArray(render_item.vao);
					glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render_item.ibo);
					glUniform1i(animated_loc, 0);
					if (render_item.animated) {
						glUniform1i(animated_loc, 1);
						auto& animmatricies = render_item.animation->animation_matrices;
						glUniformMatrix4fv(animatrix_loc, animmatricies.size(), GL_FALSE, glm::value_ptr(animmatricies[0]));
					}
					for (VertexGroup* vertex_group : *render_item.vertex_groups) {
						glUniformMatrix4fv(model_index, 1, GL_FALSE, glm::value_ptr(*render_item.model_matrix));
						glDrawElements(vertex_group->material->GetDrawElementsMode(), vertex_group->index_count, GL_UNSIGNED_INT, (GLvoid*)(vertex_group->starting_offset * sizeof(GLuint)));
					}
				}
			}
		}
		def_shader->UnUse();
		glBindVertexArray(0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	void RenderSystem::GeometryPass() {
		this->light_gbuffer.GeometyPass();


		glm::mat4 camera_matrix(1.0);
		{
			View* view = this->current_view;
			if (view) {
				camera_matrix = view->view_matrix;
			}
		}

		std::shared_ptr<Shader> def_shader = ShaderMap::Get("deferred");
		def_shader->Use();

		glUniformMatrix4fv(def_shader->GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(camera_matrix));
		glUniformMatrix4fv(def_shader->GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(this->projection));
		GLint animatrix_loc = def_shader->GetUniformLocation("animation_matrix");
		GLint animated_loc = def_shader->GetUniformLocation("animated");
		GLint model_index = def_shader->GetUniformLocation("model");
		for (auto shader_list : this->render_item_list) {
			// Check if we need to use a specific shader and set it up.
			if (shader_list.first) {
				shader_list.first->Use();
				glUniformMatrix4fv(shader_list.first->GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(camera_matrix));
				glUniformMatrix4fv(shader_list.first->GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(this->projection));
				animatrix_loc = shader_list.first->GetUniformLocation("animation_matrix");
				animated_loc = shader_list.first->GetUniformLocation("animated");
				model_index = shader_list.first->GetUniformLocation("model");
			}
			for (auto render_item : shader_list.second) {
				glBindVertexArray(render_item.vao);
				glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, render_item.ibo);
				glUniform1i(animated_loc, 0);
				if (render_item.animated) {
					glUniform1i(animated_loc, 1);
					auto& animmatricies = render_item.animation->animation_matrices;
					glUniformMatrix4fv(animatrix_loc, animmatricies.size(), GL_FALSE, glm::value_ptr(animmatricies[0]));
				}
				for (VertexGroup* vertex_group : *render_item.vertex_groups) {
					glPolygonMode(GL_FRONT_AND_BACK, vertex_group->material->GetPolygonMode());
					vertex_group->material->Activate();
					glUniformMatrix4fv(model_index, 1, GL_FALSE, glm::value_ptr(*render_item.model_matrix));
					glDrawElements(vertex_group->material->GetDrawElementsMode(), vertex_group->index_count, GL_UNSIGNED_INT, (GLvoid*)(vertex_group->starting_offset * sizeof(GLuint)));
					vertex_group->material->Deactivate();
				}
			}
			// If we used a special shader set things back to the deferred shader.
			if (shader_list.first) {
				animatrix_loc = def_shader->GetUniformLocation("animation_matrix");
				animated_loc = def_shader->GetUniformLocation("animated");
				model_index = def_shader->GetUniformLocation("model");
				shader_list.first->UnUse();
				def_shader->Use();
			}
		}
		def_shader->UnUse();
		glBindVertexArray(0);
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, 0);
	}

	void RenderSystem::PointLightPass() {
		this->light_gbuffer.LightPass(this->shadow_gbuffer.GetDepthTexture());

		std::shared_ptr<Shader> def_pl_shader = ShaderMap::Get("deferred_pointlight");
		def_pl_shader->Use();

		glm::mat4 camera_matrix(1.0);
		{
			View* view = this->current_view;
			if (view) {
				camera_matrix = view->view_matrix;
			}
		}
		glUniformMatrix4fv(def_pl_shader->GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(camera_matrix));
		glUniformMatrix4fv(def_pl_shader->GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(this->projection));
		glUniform1i(def_pl_shader->GetUniformLocation("gPositionMap"), GBuffer::GBUFFER_TEXTURE_TYPE_POSITION);
		glUniform1i(def_pl_shader->GetUniformLocation("gNormalMap"), GBuffer::GBUFFER_TEXTURE_TYPE_NORMAL);
		glUniform1i(def_pl_shader->GetUniformLocation("gColorMap"), GBuffer::GBUFFER_TEXTURE_TYPE_DIFFUSE);
		glUniform2f(def_pl_shader->GetUniformLocation("gScreenSize"), (GLfloat) this->window_width, (GLfloat) this->window_height);
		GLint model_index = def_pl_shader->GetUniformLocation("model");
		GLint Color_index = def_pl_shader->GetUniformLocation("gPointLight.Base.Color");
		GLint AmbientIntensity_index = def_pl_shader->GetUniformLocation("gPointLight.Base.AmbientIntensity");
		GLint DiffuseIntensity_index = def_pl_shader->GetUniformLocation("gPointLight.Base.DiffuseIntensity");
		GLint Atten_Constant_index = def_pl_shader->GetUniformLocation("gPointLight.Atten.Constant");
		GLint Atten_Linear_index = def_pl_shader->GetUniformLocation("gPointLight.Atten.Linear");
		GLint Atten_Exp_index = def_pl_shader->GetUniformLocation("gPointLight.Atten.Exp");

		std::shared_ptr<Shader> def_stencil_shader = ShaderMap::Get("deferred_stencil");
		def_stencil_shader->Use();
		GLint stencil_model_index = def_stencil_shader->GetUniformLocation("model");
		glUniformMatrix4fv(def_stencil_shader->GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(camera_matrix));
		glUniformMatrix4fv(def_stencil_shader->GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(this->projection));

		glBindVertexArray(this->sphere_vbo.GetVAO());
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->sphere_vbo.GetIBO());
		std::size_t index_count = this->sphere_vbo.GetVertexGroup(0)->index_count;

		for (auto itr = PointLightMap::Begin(); itr != PointLightMap::End(); ++itr) {
			eid entity_id = itr->first;
			PointLight* light = itr->second;

			Entity e(entity_id);
			glm::vec3 position, scale(1.0);
			glm::quat orientation;
			if (Multiton<eid, Position*>::Has(entity_id)) {
				position = Multiton<eid, Position*>::Get(entity_id)->value;
			}
			if (Multiton<eid, Orientation*>::Has(entity_id)) {
				orientation = Multiton<eid, Orientation*>::Get(entity_id)->value;
			}
			if (Multiton<eid, Scale*>::Has(entity_id)) {
				scale = Multiton<eid, Scale*>::Get(entity_id)->value;
			}

			glm::mat4 transform_matrix = glm::scale(glm::translate(glm::mat4(1.0), position) *
				glm::mat4_cast(orientation), scale);

			light->UpdateBoundingRadius();
			glm::mat4 scale_matrix = glm::scale(transform_matrix, glm::vec3(light->bounding_radius));

			this->light_gbuffer.StencilPass();
			def_stencil_shader->Use();
			glUniformMatrix4fv(stencil_model_index, 1, GL_FALSE, glm::value_ptr(scale_matrix));
			glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);
			def_stencil_shader->UnUse();

			// Change state for light pass after the stencil pass. Stencil pass must happen for each light.
			this->light_gbuffer.LightPass(this->shadow_gbuffer.GetDepthTexture());
			def_pl_shader->Use();
			glUniform3f(Color_index, light->color.x, light->color.y, light->color.z);
			glUniform1f(AmbientIntensity_index, light->ambient_intensity);
			glUniform1f(DiffuseIntensity_index, light->diffuse_intensity);
			glUniform1f(Atten_Constant_index, light->Attenuation.constant);
			glUniform1f(Atten_Linear_index, light->Attenuation.linear);
			glUniform1f(Atten_Exp_index, light->Attenuation.exponential);
			glUniformMatrix4fv(model_index, 1, GL_FALSE, glm::value_ptr(scale_matrix));
			glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);
		}

		def_pl_shader->UnUse();
	}

	void RenderSystem::DirectionalLightPass() {
		this->light_gbuffer.LightPass(this->shadow_gbuffer.GetDepthTexture());

		std::shared_ptr<Shader> def_dl_shader = ShaderMap::Get("deferred_dirlight");
		def_dl_shader->Use();

		glm::mat4 camera_matrix(1.0);
		{
			View* view = this->current_view;
			if (view) {
				camera_matrix = view->view_matrix;
			}
		}

		glm::mat4 ident;
		glUniformMatrix4fv(def_dl_shader->GetUniformLocation("view"), 1, GL_FALSE, glm::value_ptr(ident));
		glUniformMatrix4fv(def_dl_shader->GetUniformLocation("projection"), 1, GL_FALSE, glm::value_ptr(ident));
		glUniformMatrix4fv(def_dl_shader->GetUniformLocation("model"), 1, GL_FALSE, glm::value_ptr(ident));
		glUniformMatrix4fv(def_dl_shader->GetUniformLocation("gCameraPos"), 1, GL_FALSE, glm::value_ptr(camera_matrix));
		glUniform1i(def_dl_shader->GetUniformLocation("gPositionMap"), GBuffer::GBUFFER_TEXTURE_TYPE_POSITION);
		glUniform1i(def_dl_shader->GetUniformLocation("gNormalMap"), GBuffer::GBUFFER_TEXTURE_TYPE_NORMAL);
		glUniform1i(def_dl_shader->GetUniformLocation("gColorMap"), GBuffer::GBUFFER_TEXTURE_TYPE_DIFFUSE);
		glUniform1i(def_dl_shader->GetUniformLocation("gShadowMap"), GBuffer::GBUFFER_NUM_TEXTURES);
		glUniform2f(def_dl_shader->GetUniformLocation("gScreenSize"), (GLfloat) this->window_width, (GLfloat) this->window_height);
		glUniform3f(def_dl_shader->GetUniformLocation("gEyeWorldPos"), camera_matrix[3].x, camera_matrix[3].y, camera_matrix[3].z);
		GLint Color_index = def_dl_shader->GetUniformLocation("gDirectionalLight.Base.Color");
		GLint AmbientIntensity_index = def_dl_shader->GetUniformLocation("gDirectionalLight.Base.AmbientIntensity");
		GLint DiffuseIntensity_index = def_dl_shader->GetUniformLocation("gDirectionalLight.Base.DiffuseIntensity");
		GLint direction_index = def_dl_shader->GetUniformLocation("gDirectionalLight.Direction");
		GLint DepthBiasMVP_index = def_dl_shader->GetUniformLocation("DepthBiasMVP");

		glBindVertexArray(this->quad_vbo.GetVAO());
		glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, this->quad_vbo.GetIBO());
		glDisable(GL_CULL_FACE);

		std::size_t index_count = this->quad_vbo.GetVertexGroup(0)->index_count;
		glm::mat4 depthModelMatrix = glm::mat4(1.0);
		glm::mat4 depthProjectionMatrix = glm::ortho(-10.0, 10.0, -10.0, 10.0, -100.0, 100.0);
		glm::mat4 biasMatrix(
			0.5, 0.0, 0.0, 0.0,
			0.0, 0.5, 0.0, 0.0,
			0.0, 0.0, 0.5, 0.0,
			0.5, 0.5, 0.5, 1.0
		);

		for (auto itr = DirectionalLightMap::Begin(); itr != DirectionalLightMap::End(); ++itr) {
			DirectionalLight* light = itr->second;

			glm::vec3 lightInvDir = light->direction * -1.0f;

			// Compute the MVP matrix from the light's point of view
			glm::mat4 depthViewMatrix = glm::lookAt(lightInvDir, glm::vec3(0, 0, 0), glm::vec3(0.0, 1.0, 0.0));
			glm::mat4 depthMVP = depthProjectionMatrix * depthViewMatrix * depthModelMatrix;
			glm::mat4 depthBiasMVP = biasMatrix * depthMVP;
			glUniformMatrix4fv(DepthBiasMVP_index, 1, GL_FALSE, glm::value_ptr(depthBiasMVP));

			glUniform3f(Color_index, light->color.x, light->color.y, light->color.z);
			glUniform1f(AmbientIntensity_index, light->ambient_intensity);
			glUniform1f(DiffuseIntensity_index, light->diffuse_intensity);
			glUniform3f(direction_index, light->direction.x, light->direction.y, light->direction.z);

			glDrawElements(GL_TRIANGLES, index_count, GL_UNSIGNED_INT, 0);
		}
		def_dl_shader->UnUse();
	}

	void RenderSystem::FinalPass() {
		this->light_gbuffer.FinalPass();

		glBlitFramebuffer(0, 0, this->window_width, this->window_height,
			0, 0, this->window_width, this->window_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void RenderSystem::RenderGbuffer() {
		glBindFramebuffer(GL_FRAMEBUFFER, 0);

		this->light_gbuffer.BindForRendering();

		GLsizei HalfWidth = (GLsizei)(this->window_width / 2.0f);
		GLsizei HalfHeight = (GLsizei)(this->window_height / 2.0f);
		GLsizei QuarterWidth = (GLsizei)(this->window_width / 4.0f);
		GLsizei QuarterHeight = (GLsizei)(this->window_height / 4.0f);

		this->light_gbuffer.SetReadBuffer(GBuffer::GBUFFER_TEXTURE_TYPE_POSITION);
		glBlitFramebuffer(0, 0, this->window_width, this->window_height,
			this->window_width - QuarterWidth, 0, this->window_width, QuarterHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		this->light_gbuffer.SetReadBuffer(GBuffer::GBUFFER_TEXTURE_TYPE_DIFFUSE);
		glBlitFramebuffer(0, 0, this->window_width, this->window_height,
			this->window_width - QuarterWidth, QuarterHeight, this->window_width, HalfHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		this->light_gbuffer.SetReadBuffer(GBuffer::GBUFFER_TEXTURE_TYPE_NORMAL);
		glBlitFramebuffer(0, 0, this->window_width, this->window_height,
			this->window_width - QuarterWidth, HalfHeight, this->window_width, HalfHeight + QuarterHeight, GL_COLOR_BUFFER_BIT, GL_LINEAR);

		this->light_gbuffer.SetReadBuffer(GBuffer::GBUFFER_TEXTURE_TYPE_TEXCOORD);
		glBlitFramebuffer(0, 0, this->window_width, this->window_height,
			this->window_width - QuarterWidth, HalfHeight + QuarterHeight, this->window_width, this->window_height, GL_COLOR_BUFFER_BIT, GL_LINEAR);

	}

	typedef Multiton<eid, Renderable*> RenderableMap;
	void RenderSystem::On(std::shared_ptr<WindowResizedEvent> data) {
		SetViewportSize(data->new_width, data->new_height);
	}

	void RenderSystem::On(std::shared_ptr<EntityDestroyed> data) {
		RenderableMap::Remove(data->entity_id);
	}

	void RenderSystem::On(std::shared_ptr<EntityCreated> data) {
		eid entity_id = data->entity.id();
		for (int i = 0; i < data->entity.components_size(); ++i) {
			const proto::Component& comp = data->entity.components(i);
			switch (comp.component_case()) {
			case proto::Component::kRenderable:
			{
				Renderable* renderable = new Renderable();
				renderable->In(comp);

				RenderableMap::Set(entity_id, renderable);
			}
			break;
			}
		}
	}

	void RenderSystem::UpdateRenderList(double delta, const GameState& state) {
		this->render_item_list.clear();
		this->model_matricies.clear();

		if (!this->default_shader) {
			this->default_shader = ShaderMap::Get("debug");
		}

		// Loop through each renderbale and update its model matrix.
		for (auto itr = RenderableMap::Begin(); itr != RenderableMap::End(); ++itr) {
			eid entity_id = itr->first;
			Renderable* renderable = itr->second;
			if (renderable->hidden) {
				continue;
			}
			glm::vec3 position;
			if (state.positions.find(entity_id) != state.positions.end()) {
				position = state.positions.at(entity_id).value + state.positions.at(entity_id).center_offset;
			}
			glm::quat orientation;
			if (state.orientations.find(entity_id) != state.orientations.end()) {
				orientation = state.orientations.at(entity_id).value * glm::quat(state.orientations.at(entity_id).rotation_offset);
			}
			glm::vec3 scale(1.0);
			Entity e(entity_id);
			if (Multiton<eid, Scale*>::Has(entity_id)) {
				scale = Multiton<eid, Scale*>::Get(entity_id)->value;
			}

			this->model_matricies[entity_id] = glm::scale(glm::translate(glm::mat4(1.0), position) *
				glm::mat4_cast(orientation), scale);
			if (!renderable->buffer) {
				renderable->buffer = std::make_shared<VertexBufferObject>();
				renderable->buffer->Load(renderable->mesh);
				std::size_t group_count = renderable->buffer->GetVertexGroupCount();
				renderable->vertex_groups.clear();
				if (group_count > 0) {
					for (std::size_t i = 0; i < group_count; ++i) {
						renderable->vertex_groups.insert(renderable->buffer->GetVertexGroup(i));
					}
				}
				else {
					renderable->buffer.reset();
				}
			}

			if (renderable->buffer) {
				renderable->buffer->Update();
				RenderItem ri;
				ri.model_matrix = &this->model_matricies[entity_id];
				ri.vao = renderable->buffer->GetVAO();
				ri.ibo = renderable->buffer->GetIBO();
				ri.vertex_groups = &renderable->vertex_groups;

				if (Multiton<eid, Animation*>::Has(e.GetID())) {
					Animation* anim = Multiton<eid, Animation*>::Get(e.GetID());
					anim->UpdateAnimation(delta);
					if (anim->animation_matrices.size() > 0) {
						ri.animated = true;
						ri.animation = anim;
					}
				}
				for (VertexGroup* group : renderable->vertex_groups) {
					this->render_item_list[renderable->shader].insert(std::move(ri));
				}
			}

			for (auto itr = Multiton<eid, View*>::Begin();
				itr != Multiton<eid, View*>::End(); ++itr) {
				eid entity_id = itr->first;
				View* view = itr->second;

				glm::vec3 position;
				if (state.positions.find(entity_id) != state.positions.end()) {
					position = state.positions.at(entity_id).value;
				}
				glm::quat orientation;
				if (state.orientations.find(entity_id) != state.orientations.end()) {
					orientation = state.orientations.at(entity_id).value;
				}

				this->model_matricies[entity_id] = glm::translate(glm::mat4(1.0), position) * glm::mat4_cast(orientation);

				view->view_matrix = glm::inverse(this->model_matricies[entity_id]);
				if (view->active) {
					this->current_view = view;
				}
			}
		}
	}
}
