// Copyright (c) 2013-2016 Trillek contributors. See AUTHORS.txt for details
// Licensed under the terms of the LGPLv3. See licenses/lgpl-3.0.txt

#pragma once

#include <map>
#include <string>
#include <set>
#include <functional>

#ifdef __APPLE__
#include <OpenGL/gl3.h>
#else
#include <GL/glew.h>
#ifndef __unix
#include <GL/wglew.h>
#endif
#endif

#include <GLFW/glfw3.h>
#include <imgui.h>

#include "events.hpp"
#include "event-system.hpp"
#include "command-queue.hpp"

namespace tec {
	class IMGUISystem;
	class Console;
	class OS;

	namespace networking {
		class ServerConnection;
	}

	class IMGUISystem :
		public CommandQueue < IMGUISystem >,
		public EventQueue < KeyboardEvent >,
		public EventQueue < MouseMoveEvent >,
		public EventQueue < MouseScrollEvent >,
		public EventQueue < WindowResizedEvent > {
		typedef Command<IMGUISystem> GUICommand;
	public:
		IMGUISystem(GLFWwindow* window);
		~IMGUISystem();

		void CreateGUI(OS* os, networking::ServerConnection* connection, Console* console);

		void Update(double delta);

		void CreateDeviceObjects();

		void AddWindowDrawFunction(std::string name, std::function<void()>&& func);

		void ShowWindow(const std::string name) {
			GUICommand show_window(
				[=] (IMGUISystem*) {
					this->visible_windows.insert(name);
				});
			IMGUISystem::QueueCommand(std::move(show_window));
		}

		void HideWindow(const std::string name) {
			GUICommand hide_window(
				[=] (IMGUISystem*) {
					this->visible_windows.erase(name);
				});
			IMGUISystem::QueueCommand(std::move(hide_window));
		}

		bool IsWindowVisible(const std::string& name) const {
			return this->visible_windows.find(name) != this->visible_windows.end();
		}

		static const char* GetClipboardText(void* user_data);
		static void SetClipboardText(void* user_data, const char* text);
		static void RenderDrawLists(ImDrawData* draw_data);
	private:
		void On(std::shared_ptr<WindowResizedEvent> data);
		void On(std::shared_ptr<MouseMoveEvent > data);
		void On(std::shared_ptr<MouseScrollEvent > data);
		void On(std::shared_ptr<KeyboardEvent> data);

		void UpdateDisplaySize();

		int framebuffer_width{ 0 }, framebuffer_height{ 0 };
		int window_width{ 0 }, window_height{ 0 };
		bool mouse_pressed[3]{ false, false, false };
		ImVec2 mouse_pos{ 0, 0 };
		ImVec2 mouse_wheel{ 0, 0 };

		static GLuint font_texture;
		static GLFWwindow* window;
		static int shader_program, vertex_shader, fragment_shader;
		static int texture_attribute_location, projmtx_attribute_location;
		static int position_attribute_location, uv_attribute_location, color_attribute_location;
		static std::size_t vbo_size, ibo_size;
		static unsigned int vbo, ibo, vao;

		std::set<std::string> visible_windows;

		std::map<std::string, std::function<void()>> window_draw_funcs;
	};
}
