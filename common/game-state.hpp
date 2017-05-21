﻿// Copyright (c) 2013-2016 Trillek contributors. See AUTHORS.txt for details
// Licensed under the terms of the LGPLv3. See licenses/lgpl-3.0.txt

#pragma once

#include <map>
#include <functional>
#include <list>
#include "types.hpp"
#include "game_state.pb.h"
#include "components/transforms.hpp"
#include "components/velocity.hpp"
#include "events.hpp"

namespace tec {
	struct GameState {
		std::unordered_map<eid, Position> positions;
		std::unordered_map<eid, Orientation> orientations;
		std::unordered_map<eid, Velocity> velocities;
		std::list<std::function<void(void)>> commands;

		GameState() : state_id(0) { }

		GameState(const GameState& other) {
			if (&other == this) {
				return;
			}
			this->positions = other.positions;
			this->orientations = other.orientations;
			this->velocities = other.velocities;
			this->state_id = other.state_id;
		}
		GameState(GameState&& other) {
			this->positions = std::move(other.positions);
			this->orientations = std::move(other.orientations);
			this->velocities = std::move(other.velocities);
			this->state_id = other.state_id;
		}

		GameState& operator=(const GameState& other) {
			if (&other == this) {
				return *this;
			}
			this->positions = other.positions;
			this->orientations = other.orientations;
			this->velocities = other.velocities;
			this->state_id = other.state_id;
			return *this;
		}
		GameState& operator=(GameState&& other) {
			if (this != &other) {
				this->positions = std::move(other.positions);
				this->orientations = std::move(other.orientations);
				this->velocities = std::move(other.velocities);
				this->state_id = other.state_id;
			}
			return *this;
		}

		void In(const proto::GameStateUpdate& gsu) {
			this->state_id = gsu.state_id();
			for (int e = 0; e < gsu.entity_size(); ++e) {
				const proto::Entity& entity = gsu.entity(e);
				eid entity_id = entity.id();
				for (int i = 0; i < entity.components_size(); ++i) {
					const proto::Component& comp = entity.components(i);
					switch (comp.component_case()) {
						case proto::Component::kPosition:
						{
							Position pos;
							pos.In(comp);
							this->positions[entity_id] = pos;
						}
						break;
						case proto::Component::kOrientation:
						{
							Orientation orientation;
							orientation.In(comp);
							this->orientations[entity_id] = orientation;
						}
						break;
						case proto::Component::kVelocity:
						{
							Velocity vel;
							vel.In(comp);
							this->velocities[entity_id] = vel;
						}
						break;
						default:
						// intentionally not handling other cases.
						break;
					}
				}
			}
		}

		void Out(proto::GameStateUpdate* gsu) const {
			gsu->set_state_id(this->state_id);
			for (auto pos : this->positions) {
				tec::proto::Entity* entity = gsu->add_entity();
				entity->set_id(pos.first);
				pos.second.Out(entity->add_components());
				if (this->orientations.find(pos.first) != this->orientations.end()) {
					tec::Orientation ori = this->orientations.at(pos.first);
					ori.Out(entity->add_components());
				}
				if (this->velocities.find(pos.first) != this->velocities.end()) {
					tec::Velocity vel = this->velocities.at(pos.first);
					vel.Out(entity->add_components());
				}
			}
		}
		state_id_t state_id;
	};

	struct NewGameStateEvent {
		GameState new_state;
	};

	struct EventList {
		std::list<KeyboardEvent> keyboard_events;
		std::list<MouseBtnEvent> mouse_button_events;
		std::list<MouseMoveEvent> mouse_move_events;
		std::list<MouseClickEvent> mouse_click_events;

		EventList() { }

		EventList(const EventList&) = delete;
		EventList(EventList&& other) {
			this->keyboard_events = std::move(other.keyboard_events);
			this->mouse_button_events = std::move(other.mouse_button_events);
			this->mouse_move_events = std::move(other.mouse_move_events);
			this->mouse_click_events = std::move(other.mouse_click_events);
		}

		EventList& operator=(const EventList& other) = delete;
		EventList& operator=(EventList&& other) {
			if (this != &other) {
				this->keyboard_events = std::move(other.keyboard_events);
				this->mouse_button_events = std::move(other.mouse_button_events);
				this->mouse_move_events = std::move(other.mouse_move_events);
				this->mouse_click_events = std::move(other.mouse_click_events);
			}
			return *this;
		}
	};
}
