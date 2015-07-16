#include "os.hpp"
#include "entity.hpp"
#include "render-system.hpp"
#include "physics-system.hpp"
#include "sound-system.hpp"
#include "imgui-system.hpp"
#include "component-update-system.hpp"
#include "controllers/fps-controller.hpp"

#include <thread>

namespace tec {
	extern void IntializeComponents();
	extern void IntializeIOFunctors();
	extern void BuildTestEntities();
	extern void ProtoSave();
	extern void ProtoLoad();
	ReflectionEntityList Entity::entity_list;
}

std::list<std::function<void(tec::frame_id_t)>> tec::ComponentUpdateSystemList::update_funcs;

int main(int argc, char* argv[]) {
	tec::OS os;

	os.InitializeWindow(800, 600, "TEC 0.1", 3, 2);

	tec::IMGUISystem gui(os.GetWindow());
	ImVec4 clear_color = ImColor(114, 144, 154);
	gui.AddWindowDrawFunction("test", [&clear_color] () {
		static float f = 0.0f;
		ImGui::Text("Hello, world!");
		ImGui::SliderFloat("float", &f, 0.0f, 1.0f);
		ImGui::ColorEdit3("clear color", (float*)&clear_color);
		ImGui::Text("Application average %.3f ms/frame (%.1f FPS)", 1000.0f / ImGui::GetIO().Framerate, ImGui::GetIO().Framerate);
	});

	tec::RenderSystem rs;

	rs.SetViewportSize(800, 600);

	tec::PhysicsSystem ps;

	tec::SoundSystem ss;

	std::int64_t frame_id = 1;

	tec::IntializeComponents();
	tec::IntializeIOFunctors();
	tec::BuildTestEntities();
	tec::ProtoLoad();

	tec::FPSController camera_controller(1);

	tec::eid active_entity;
	gui.AddWindowDrawFunction("active_entity", [&active_entity] () {
		if (active_entity != 0) {
			ImGui::SetTooltip("#%i", active_entity);
		}
	});
	gui.AddWindowDrawFunction("entity_tree", [ ] () {
		static bool no_titlebar = false;
		static bool no_border = true;
		static bool no_resize = false;
		static bool no_move = false;
		static bool no_scrollbar = false;
		static bool no_collapse = false;
		static bool no_menu = true;
		static float bg_alpha = 0.65f;

		// Demonstrate the various window flags. Typically you would just use the default.
		ImGuiWindowFlags window_flags = 0;
		if (no_titlebar)  window_flags |= ImGuiWindowFlags_NoTitleBar;
		if (!no_border)   window_flags |= ImGuiWindowFlags_ShowBorders;
		if (no_resize)    window_flags |= ImGuiWindowFlags_NoResize;
		if (no_move)      window_flags |= ImGuiWindowFlags_NoMove;
		if (no_scrollbar) window_flags |= ImGuiWindowFlags_NoScrollbar;
		if (no_collapse)  window_flags |= ImGuiWindowFlags_NoCollapse;
		if (!no_menu)     window_flags |= ImGuiWindowFlags_MenuBar;
		bool opened = true;
		if (ImGui::Begin("Entity Tree", &opened, ImVec2(550, 680), bg_alpha, window_flags)) {
			if (ImGui::TreeNode("Entities")) {
				for (const auto& entity : tec::Entity::entity_list.entities) {
					if (ImGui::TreeNode((void*)entity.first, "#%d", entity.first)) {
						int i = 0;
						for (const auto& component : entity.second.components) {
							if (ImGui::TreeNode((void*)i++, component.first.c_str())) {
								for (const auto& prop : component.second.properties) {
									ImGui::Text((prop.first + ": " + prop.second).c_str());
								}
								ImGui::TreePop();
							}
						}
						ImGui::TreePop();
					}
				}
				ImGui::TreePop();
			}
			ImGui::End();
		}
	});

	double delta = os.GetDeltaTime();
	while (!os.Closing()) {
		os.OSMessageLoop();
		delta = os.GetDeltaTime();

		tec::ComponentUpdateSystemList::UpdateAll(frame_id);

		camera_controller.Update(delta);
		std::thread ps_thread([&] () {
			ps.Update(delta);
		});
		std::thread ss_thread([&] () {
			ss.Update(delta);
		});

		rs.Update(delta);

		os.OSMessageLoop();

		ps_thread.join();
		ss_thread.join();

		ps.DebugDraw();

		gui.Update(delta);

		os.SwapBuffers();
		frame_id++;
		active_entity = ps.RayCast();
	}
	tec::ProtoSave();

	return 0;
}
