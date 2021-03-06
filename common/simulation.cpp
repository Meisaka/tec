#include "simulation.hpp"

#include <future>
#include <iostream>
#include <set>
#include <thread>

#include <commands.pb.h>

#include "components/transforms.hpp"
#include "controllers/fps-controller.hpp"

namespace tec {

double UPDATE_RATE = 1.0 / 8.0; // 8 per second
double TICKS_PER_SECOND = 60.0 * UPDATE_RATE;
GameState Simulation::Simulate(const double delta_time, GameState& interpolated_state) {
	ProcessCommandQueue();
	EventQueue<KeyboardEvent>::ProcessEventQueue();
	EventQueue<MouseBtnEvent>::ProcessEventQueue();
	EventQueue<MouseMoveEvent>::ProcessEventQueue();
	EventQueue<MouseClickEvent>::ProcessEventQueue();
	EventQueue<ClientCommandsEvent>::ProcessEventQueue();
	EventQueue<ControllerAddedEvent>::ProcessEventQueue();
	EventQueue<ControllerRemovedEvent>::ProcessEventQueue();
	EventQueue<FocusCapturedEvent>::ProcessEventQueue();
	EventQueue<FocusBlurEvent>::ProcessEventQueue();

	auto vcomp_future = std::async(std::launch::async, [&]() { vcomp_sys.Update(delta_time); });

	for (Controller* controller : this->controllers) {
		controller->Update(delta_time, interpolated_state, this->event_list);
	}

	this->event_list.mouse_button_events.clear();
	this->event_list.mouse_move_events.clear();
	this->event_list.keyboard_events.clear();
	this->event_list.mouse_click_events.clear();

	GameState client_state = interpolated_state;
	std::future<std::set<eid>> phys_future =
			std::async(std::launch::async, [=, &interpolated_state]() -> std::set<eid> {
				return phys_sys.Update(delta_time, interpolated_state);
			});
	std::set<eid> phys_results = phys_future.get();

	if (phys_results.size() > 0) {
		for (eid entity_id : phys_results) {
			client_state.positions[entity_id] = this->phys_sys.GetPosition(entity_id);
			client_state.orientations[entity_id] = this->phys_sys.GetOrientation(entity_id);
			if (interpolated_state.velocities.find(entity_id) != interpolated_state.velocities.end()) {
				client_state.velocities[entity_id] = interpolated_state.velocities.at(entity_id);
			}
		}
	}
	vcomp_future.wait();

	return client_state;
}

void Simulation::AddController(Controller* controller) { this->controllers.push_back(controller); }

void Simulation::RemoveController(Controller* controller) { this->controllers.remove(controller); }

void Simulation::On(std::shared_ptr<KeyboardEvent> data) { this->event_list.keyboard_events.push_back(*data.get()); }

void Simulation::On(std::shared_ptr<MouseBtnEvent> data) {
	this->event_list.mouse_button_events.push_back(*data.get());
}

void Simulation::On(std::shared_ptr<MouseMoveEvent> data) { this->event_list.mouse_move_events.push_back(*data.get()); }

void Simulation::On(std::shared_ptr<MouseClickEvent> data) {
	this->event_list.mouse_click_events.push_back(*data.get());
}

void Simulation::On(std::shared_ptr<ControllerAddedEvent> data) { AddController(data->controller.get()); }

void Simulation::On(std::shared_ptr<ControllerRemovedEvent> data) { RemoveController(data->controller.get()); }

/// \brief This event is sent to indicate that focus had been captured by an entity
/// it specifies which was captured: keyboard or mouse, if either of these parameters is true,
/// then prevent normal processing of those events
/// a controller implementing this should logically AND NOT these inputs with it's current settings
void Simulation::On(std::shared_ptr<FocusCapturedEvent> data) {
	for (Controller* controller : this->controllers) {
		controller->ClearFocus(data->keyboard, data->mouse);
	}
}

/// \brief This event is sent to indicate that focus had been released (blur) from an owning entity
/// it specifies which was blurred: keyboard or mouse, if either of these parameters is true,
/// then restore normal processing of those events
/// a controller implementing this should logically OR these inputs with it's current settings
void Simulation::On(std::shared_ptr<FocusBlurEvent> data) {
	for (Controller* controller : this->controllers) {
		controller->SetFocus(data->keyboard, data->mouse);
	}
}

void Simulation::On(std::shared_ptr<ClientCommandsEvent> data) {
	for (Controller* controller : this->controllers) {
		if (static_cast<unsigned int>(controller->entity_id) == data->client_commands.id()) {
			controller->ApplyClientCommands(data->client_commands);
		}
	}
}

} // end namespace tec
