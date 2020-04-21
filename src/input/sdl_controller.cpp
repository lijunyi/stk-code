//  SuperTuxKart - a fun racing game with go-kart
//  Copyright (C) 2020 SuperTuxKart-Team
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  as published by the Free Software Foundation; either version 3
//  of the License, or (at your option) any later version.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, write to the Free Software
//  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

#ifndef SERVER_ONLY

#include "input/sdl_controller.hpp"
#include "input/device_config.hpp"
#include "input/device_manager.hpp"
#include "input/gamepad_device.hpp"
#include "input/input_manager.hpp"
#include "utils/log.hpp"

#include <stdexcept>
#include <string>

// ----------------------------------------------------------------------------
SDLController::SDLController(int device_id)
             : m_gamepad(NULL)
{
    m_irr_event = {};
    m_irr_event.EventType = irr::EET_JOYSTICK_INPUT_EVENT;
    memset(m_prev_axes, 0,
        irr::SEvent::SJoystickEvent::NUMBER_OF_AXES * sizeof(int16_t));
    m_game_controller = NULL;
    m_joystick = NULL;
    m_id = -1;

    if (SDL_IsGameController(device_id))
    {
        m_game_controller = SDL_GameControllerOpen(device_id);
        if (!m_game_controller)
            throw std::runtime_error(SDL_GetError());
        m_joystick = SDL_GameControllerGetJoystick(m_game_controller);
        if (!m_joystick)
        {
            SDL_GameControllerClose(m_game_controller);
            throw std::runtime_error(SDL_GetError());
        }
    }
    else
    {
        m_joystick = SDL_JoystickOpen(device_id);
        if (!m_joystick)
            throw std::runtime_error(SDL_GetError());
    }

    m_id = SDL_JoystickInstanceID(m_joystick);
    if (m_id < 0)
    {
        if (m_game_controller)
            SDL_GameControllerClose(m_game_controller);
        else
            SDL_JoystickClose(m_joystick);
        throw std::runtime_error(SDL_GetError());
    }

    m_irr_event.JoystickEvent.Joystick = m_id;
    const char* name_cstr = SDL_JoystickName(m_joystick);
    if (name_cstr == NULL)
    {
        if (m_game_controller)
            SDL_GameControllerClose(m_game_controller);
        else
            SDL_JoystickClose(m_joystick);
        throw std::runtime_error("missing name for joystick");
    }
    std::string name = name_cstr;

    m_buttons = SDL_JoystickNumButtons(m_joystick);
    if (m_buttons < 0)
    {
        if (m_game_controller)
            SDL_GameControllerClose(m_game_controller);
        else
            SDL_JoystickClose(m_joystick);
        throw std::runtime_error(SDL_GetError());
    }

    m_axes = SDL_JoystickNumAxes(m_joystick);
    if (m_axes < 0)
    {
        if (m_game_controller)
            SDL_GameControllerClose(m_game_controller);
        else
            SDL_JoystickClose(m_joystick);
        throw std::runtime_error(SDL_GetError());
    }

    m_hats = SDL_JoystickNumHats(m_joystick);
    if (m_hats < 0)
    {
        if (m_game_controller)
            SDL_GameControllerClose(m_game_controller);
        else
            SDL_JoystickClose(m_joystick);
        throw std::runtime_error(SDL_GetError());
    }

    Log::info("SDLController",
        "%s plugged in: buttons: %d, axes: %d, hats: %d.", name.c_str(),
        m_buttons, m_axes, m_hats);
    if (m_game_controller && SDL_GameControllerName(m_game_controller))
    {
        Log::info("SDLController", "%s uses game controller mapping %s.",
            name.c_str(), SDL_GameControllerName(m_game_controller));
    }

    if (m_buttons > irr::SEvent::SJoystickEvent::NUMBER_OF_BUTTONS)
        m_buttons = irr::SEvent::SJoystickEvent::NUMBER_OF_BUTTONS;
    if (m_axes > irr::SEvent::SJoystickEvent::NUMBER_OF_AXES)
        m_axes = irr::SEvent::SJoystickEvent::NUMBER_OF_AXES;
    // We store hats event with 4 buttons
    int max_buttons_with_hats =
        irr::SEvent::SJoystickEvent::NUMBER_OF_BUTTONS - (m_hats * 4);
    if (m_buttons > max_buttons_with_hats)
        m_hats = 0;
    else
        m_buttons += m_hats * 4;
    // Save previous axes values for input sensing
    for (int i = 0; i < m_axes; i++)
        m_prev_axes[i] = SDL_JoystickGetAxis(m_joystick, i);

    DeviceManager* dm = input_manager->getDeviceManager();
    GamepadConfig* cfg = NULL;
    bool created = false;

    if (dm->getConfigForGamepad(m_id, name, &cfg) == true)
    {
        Log::info("SDLController", "creating new configuration for %s.",
            name.c_str());
        created = true;
    }

    std::string mapping_string;
    if (m_game_controller)
    {
        char* mapping = SDL_GameControllerMapping(m_game_controller);
        if (mapping)
        {
            mapping_string = mapping;
            SDL_free(mapping);
        }
    }
    cfg->initSDLController(mapping_string, m_buttons, m_axes, m_hats);
    cfg->setPlugged();
    for (int i = 0; i < dm->getGamePadAmount(); i++)
    {
        GamePadDevice* d = dm->getGamePad(i);
        if (d->getName() == name && !d->isConnected())
        {
            m_gamepad = d;
            d->setConnected(true);
            d->setIrrIndex(m_id);
            d->setConfiguration(cfg);
            if (created)
                dm->save();
            return;
        }
    }

    m_gamepad = new GamePadDevice(m_id, name, m_axes, m_buttons, cfg);
    dm->addGamepad(m_gamepad);
    if (created)
        dm->save();
}   // SDLController

// ----------------------------------------------------------------------------
SDLController::~SDLController()
{
    Log::info("SDLController", "%s unplugged.", SDL_JoystickName(m_joystick));
    if (m_game_controller)
        SDL_GameControllerClose(m_game_controller);
    else
        SDL_JoystickClose(m_joystick);
    m_gamepad->getConfiguration()->unPlugged();
    m_gamepad->setIrrIndex(-1);
    m_gamepad->setConnected(false);
}   // ~SDLController

// ----------------------------------------------------------------------------
/** SDL only sends event when axis moves, so we need to send previously saved
 *  event for correct input sensing. */
void SDLController::handleAxisInputSense(const SDL_Event& event)
{
    int axis_idx = event.jaxis.axis;
    if (axis_idx > m_axes)
        return;
    if (event.jaxis.value == m_prev_axes[axis_idx])
        return;
    input_manager->dispatchInput(Input::IT_STICKMOTION,
        m_irr_event.JoystickEvent.Joystick, axis_idx, Input::AD_NEUTRAL,
        m_prev_axes[axis_idx]);
}   // handleAxisInputSense

#endif