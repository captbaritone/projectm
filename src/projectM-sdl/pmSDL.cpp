/**
* projectM -- Milkdrop-esque visualisation SDK
* Copyright (C)2003-2019 projectM Team
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
* See 'LICENSE.txt' included within this release
*
* projectM-sdl
* This is an implementation of projectM using libSDL2
*
* pmSDL.cpp
* Authors: Created by Mischa Spiegelmock on 2017-09-18.
*
*
* experimental Stereoscopic SBS driver functionality by
*	RobertPancoast77@gmail.com
*/

#include "pmSDL.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>
#include "Renderer/ShaderEngine.hpp"
#include "Renderer/StaticGlShaders.h"


void projectMSDL::audioInputCallbackF32(void *userdata, unsigned char *stream, int len) {
    projectMSDL *app = (projectMSDL *) userdata;
//    printf("\nLEN: %i\n", len);
//    for (int i = 0; i < 64; i++)
//        printf("%X ", stream[i]);
    // stream is (i think) samples*channels floats (native byte order) of len BYTES
    if (app->audioChannelsCount == 1)
        app->pcm()->addPCMfloat((float *)stream, len/sizeof(float));
    else if (app->audioChannelsCount == 2)
        app->pcm()->addPCMfloat_2ch((float *)stream, len/sizeof(float));
    else {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Multichannel audio not supported");
        SDL_Quit();
    }
}

void projectMSDL::audioInputCallbackS16(void *userdata, unsigned char *stream, int len) {
    //    printf("LEN: %i\n", len);
    projectMSDL *app = (projectMSDL *) userdata;
    short pcm16[2][512];

    for (int i = 0; i < 512; i++) {
        for (int j = 0; j < app->audioChannelsCount; j++) {
            pcm16[j][i] = stream[i+j];
        }
    }
    app->pcm()->addPCM16(pcm16);
}

int projectMSDL::toggleAudioInput() {
    // trigger a toggle with CMD-I or CTRL-I
    if (wasapi) { // we are currently on WASAPI, so we are going to revert to a microphone/line-in input.
        if (this->openAudioInput())
            this->beginAudioCapture();
        CurAudioDevice = 0;
        selectedAudioDevice = CurAudioDevice;
        this->wasapi = false; // Track wasapi as off so projectMSDL will stop listening to WASAPI loopback in pmSDL_main.
    }
    else {
        this->endAudioCapture(); // end current audio capture.
        CurAudioDevice++; // iterate device index
        if (CurAudioDevice >= NumAudioDevices) { // We reached outside the boundaries of available audio devices.
            CurAudioDevice = 0; // Return to first audio device in the index.
#ifdef WASAPI_LOOPBACK
            // If we are at the boundary and WASAPI is enabled then let's load WASAPI instead.
            projectM::setToastMessage("Loopback audio selected");
            SDL_Log("Loopback audio selected");
            this->fakeAudio = false; // disable fakeAudio in case it was enabled.
            this->wasapi = true; // Track wasapi as on so projectMSDL will listen to it.
#else
            if (NumAudioDevices == 1) // If WASAPI_LOOPBACK was not enabled and there is only one audio device, it's pointless to toggle anything.
            {
                SDL_Log("There is only one audio capture device. There is nothing to toggle at this time.");
                return 1;
            }
            // If WASAPI_LOOPBACK is not enabled and we have multiple input devices, return to device index 0 and let's listen to that device.
            selectedAudioDevice = CurAudioDevice;
            initAudioInput();
            this->beginAudioCapture();
#endif
        }
        else {
            // This is a normal scenario where we move forward in the audio device index.
            selectedAudioDevice = CurAudioDevice;
            initAudioInput();
            this->beginAudioCapture();
        }
    }
    return 1;
}

int projectMSDL::initAudioInput() {


    // params for audio input
    SDL_AudioSpec want, have;

    // requested format
    // https://wiki.libsdl.org/SDL_AudioSpec#Remarks
    SDL_zero(want);
    want.freq = 44100;
    want.format = AUDIO_F32;  // float
    want.channels = 2;
    want.samples = PCM::maxsamples;
    want.callback = projectMSDL::audioInputCallbackF32;
    want.userdata = this;

    audioDeviceID = SDL_OpenAudioDevice(SDL_GetAudioDeviceName(selectedAudioDevice, true), true, &want, &have, 0);

    if (audioDeviceID == 0) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "Failed to open audio capture device: %s", SDL_GetError());
        return 0;
    }

    // read characteristics of opened capture device
    SDL_Log("Opened audio capture device index=%i devId=%i: %s", selectedAudioDevice, audioDeviceID, SDL_GetAudioDeviceName(selectedAudioDevice, true));
    std::string deviceToast = SDL_GetAudioDeviceName(selectedAudioDevice, true); // Example: Microphone rear
    deviceToast += " selected";
    projectM::setToastMessage(deviceToast);
#ifdef DEBUG
    SDL_Log("Samples: %i, frequency: %i, channels: %i, format: %i", have.samples, have.freq, have.channels, have.format);
#endif
    audioChannelsCount = have.channels;
    audioSampleRate = have.freq;
    audioSampleCount = have.samples;
    audioFormat = have.format;
    audioInputDevice = audioDeviceID;

    return 1;
}

int projectMSDL::openAudioInput() {
    fakeAudio = false; // if we are opening an audio input then there is no need for fake audio.
    // get audio driver name (static)
#ifdef DEBUG
    const char* driver_name = SDL_GetCurrentAudioDriver();
    SDL_Log("Using audio driver: %s\n", driver_name);
#endif

    // get audio input device
    NumAudioDevices = SDL_GetNumAudioDevices(true);  // capture, please

    CurAudioDevice = 0;
    if (NumAudioDevices == 0) {
        SDL_LogCritical(SDL_LOG_CATEGORY_APPLICATION, "No audio capture devices found");
        projectM::setToastMessage("No audio capture devices found: using simulated audio");
        fakeAudio = true;
        return 0;
    }
#ifdef DEBUG
    for (unsigned int i = 0; i < NumAudioDevices; i++) {
        SDL_Log("Found audio capture device %d: %s", i, SDL_GetAudioDeviceName(i, true));
    }
#endif

    // default selected Audio Device to 0.
    selectedAudioDevice = 0;
    initAudioInput();

    return 1;
}

void projectMSDL::beginAudioCapture() {
    // allocate a buffer to store PCM data for feeding in
    SDL_PauseAudioDevice(audioDeviceID, false);
}

void projectMSDL::endAudioCapture() {
    SDL_PauseAudioDevice(audioDeviceID, true);
    SDL_CloseAudioDevice(audioDeviceID);
}

void projectMSDL::setHelpText(const std::string & helpText) {
    projectM::setHelpText(helpText);
}

void projectMSDL::maximize() {
    SDL_DisplayMode dm;
    if (SDL_GetDesktopDisplayMode(0, &dm) != 0) {
        SDL_Log("SDL_GetDesktopDisplayMode failed: %s", SDL_GetError());
        return;
    }

    SDL_SetWindowSize(win, dm.w, dm.h);
    resize(dm.w, dm.h);
}

/* Stretch projectM across multiple monitors */
void projectMSDL::stretchMonitors()
{
	int displayCount = SDL_GetNumVideoDisplays();
	if (displayCount >= 2)
	{
		std::vector<SDL_Rect> displayBounds;
		for (int i = 0; i < displayCount; i++)
		{
			displayBounds.push_back(SDL_Rect());
			SDL_GetDisplayBounds(i, &displayBounds.back());
		}

		int mostXLeft = 0;
		int mostXRight = 0;
		int mostWide = 0;
		int mostYUp = 0;
		int mostYDown = 0;
		int mostHigh = 0;

		for (int i = 0; i < displayCount; i++)
		{
			if (displayBounds[i].x < mostXLeft) mostXLeft = displayBounds[i].x;
			if ((displayBounds[i].x + displayBounds[i].w) > mostXRight) mostXRight = displayBounds[i].x + displayBounds[i].w;
		}
		for (int i = 0; i < displayCount; i++)
		{
			if (displayBounds[i].y < mostYUp) mostYUp = displayBounds[i].y;
			if ((displayBounds[i].y + displayBounds[i].h) > mostYDown) mostYDown = displayBounds[i].y + displayBounds[i].h;
		}

        mostWide = abs(mostXLeft) + abs(mostXRight);
        mostHigh = abs(mostYUp) + abs(mostYDown);

		SDL_SetWindowPosition(win, mostXLeft, mostYUp);
		SDL_SetWindowSize(win, mostWide, mostHigh);
	}
}

/* Moves projectM to the next monitor */
void projectMSDL::nextMonitor()
{
	int displayCount = SDL_GetNumVideoDisplays();
	int currentWindowIndex = SDL_GetWindowDisplayIndex(win);
	if (displayCount >= 2)
	{
		std::vector<SDL_Rect> displayBounds;
		int nextWindow = currentWindowIndex + 1;
		if (nextWindow >= displayCount) nextWindow = 0;

		for (int i = 0; i < displayCount; i++)
		{
			displayBounds.push_back(SDL_Rect());
			SDL_GetDisplayBounds(i, &displayBounds.back());
		}
		SDL_SetWindowPosition(win, displayBounds[nextWindow].x, displayBounds[nextWindow].y);
		SDL_SetWindowSize(win, displayBounds[nextWindow].w, displayBounds[nextWindow].h);
	}
}

void projectMSDL::toggleFullScreen() {
    maximize();
    if (isFullScreen) {
        SDL_SetWindowFullscreen(win, 0);
        isFullScreen = false;
        SDL_ShowCursor(true);
    } else {
        SDL_ShowCursor(false);
        SDL_SetWindowFullscreen(win, SDL_WINDOW_FULLSCREEN);
        isFullScreen = true;
    }
}

void projectMSDL::scrollHandler(SDL_Event* sdl_evt) {
     // handle mouse scroll wheel - up++
    if (sdl_evt->wheel.y > 0) {
        projectM::selectPrevious(true);
    }
    // handle mouse scroll wheel - down--
    if (sdl_evt->wheel.y < 0) {
        projectM::selectNext(true);
    }
}

void projectMSDL::keyHandler(SDL_Event *sdl_evt) {
    projectMEvent evt;
    projectMKeycode key;
    projectMModifier mod;
    SDL_Keymod sdl_mod = (SDL_Keymod) sdl_evt->key.keysym.mod;
    SDL_Keycode sdl_keycode = sdl_evt->key.keysym.sym;

    // Left or Right Gui or Left Ctrl
    if (sdl_mod & KMOD_LGUI || sdl_mod & KMOD_RGUI || sdl_mod & KMOD_LCTRL)
        keymod = true;

	// handle keyboard input (for our app first, then projectM)
    switch (sdl_keycode) {

    case SDLK_q:
        if (sdl_mod & KMOD_LGUI || sdl_mod & KMOD_RGUI || sdl_mod & KMOD_LCTRL) {
            // cmd/ctrl-q = quit
            done = 1;
            return;
        }
        break;
    case SDLK_BACKSPACE:
        projectM::deleteSearchText();
        break;
    case SDLK_SLASH:
        break;
    case SDLK_BACKSLASH:
        break;
    case SDLK_RETURN:
        if (!projectM::isTextInputActive()) {
            SDL_StartTextInput();
        }
        break;
    case SDLK_ESCAPE:
        if (projectM::isTextInputActive())
            SDL_StopTextInput();
        break;
    case SDLK_i:
        if (sdl_mod & KMOD_LGUI || sdl_mod & KMOD_RGUI || sdl_mod & KMOD_LCTRL)
        {
            toggleAudioInput();
            return; // handled
        }
        break;
    case SDLK_s:
        if (sdl_mod & KMOD_LGUI || sdl_mod & KMOD_RGUI || sdl_mod & KMOD_LCTRL)
        {
            // command-s: [s]tretch monitors
            // Stereo requires fullscreen
#if !STEREOSCOPIC_SBS
            if (!this->stretch) { // if stretching is not already enabled, enable it.
                stretchMonitors();
                this->stretch = true;
            } else {
                toggleFullScreen(); // else, just toggle full screen so we leave stretch mode.
                this->stretch = false;
            }
#endif
            return; // handled
        }
    case SDLK_m:
        if (sdl_mod & KMOD_LGUI || sdl_mod & KMOD_RGUI || sdl_mod & KMOD_LCTRL)
        {
            // command-m: change [m]onitor
            // Stereo requires fullscreen
#if !STEREOSCOPIC_SBS
            nextMonitor();
#endif
            this->stretch = false; // if we are switching monitors, ensure we disable monitor stretching.
            return; // handled
        }
    case SDLK_f:
        if (sdl_mod & KMOD_LGUI || sdl_mod & KMOD_RGUI || sdl_mod & KMOD_LCTRL) {
            // command-f: fullscreen
            // Stereo requires fullscreen
#if !STEREOSCOPIC_SBS
            toggleFullScreen();
#endif
            this->stretch = false; // if we are toggling fullscreen, ensure we disable monitor stretching.
            return; // handled
        }
        break;
    case SDLK_LEFT:
        // selectPrevious(true);
        break;
    case SDLK_RIGHT:
        // selectNext(true);
        break;
    case SDLK_UP:
        break;
    case SDLK_DOWN:
        break;

    case SDLK_F3:
        break;


    case SDLK_SPACE:
        if (!projectM::isTextInputActive(true))
            setPresetLock(!isPresetLocked());
        break;
    case SDLK_F1:
        break;
    case SDLK_DELETE:
        /*
        try {
            if (selectedPresetIndex(index)) {
                DeleteFile(
                    LPCSTR(
                        getPresetURL(index).c_str()
                    )
                );
            }
        }
        catch (const std::exception & e) {
            printf("Delete failed");
        }
        */
        break;
    }
    // translate into projectM codes and perform default projectM handler
    evt = sdl2pmEvent(sdl_evt);
    mod = sdl2pmModifier(sdl_mod);
    key = sdl2pmKeycode(sdl_keycode,sdl_mod);
    key_handler(evt, key, mod);
}

void projectMSDL::addFakePCM() {
    int i;
    short pcm_data[2][512];
    /** Produce some fake PCM data to stuff into projectM */
    for ( i = 0 ; i < 512 ; i++ ) {
        if ( i % 2 == 0 ) {
            pcm_data[0][i] = (float)( rand() / ( (float)RAND_MAX ) * (pow(2,14) ) );
            pcm_data[1][i] = (float)( rand() / ( (float)RAND_MAX ) * (pow(2,14) ) );
        } else {
            pcm_data[0][i] = (float)( rand() / ( (float)RAND_MAX ) * (pow(2,14) ) );
            pcm_data[1][i] = (float)( rand() / ( (float)RAND_MAX ) * (pow(2,14) ) );
        }
        if ( i % 2 == 1 ) {
            pcm_data[0][i] = -pcm_data[0][i];
            pcm_data[1][i] = -pcm_data[1][i];
        }
    }

    /** Add the waveform data */
    pcm()->addPCM16(pcm_data);
}

void projectMSDL::resize(unsigned int width_, unsigned int height_) {
    width = width_;
    height = height_;
    projectM_resetGL(width, height);
}

void projectMSDL::pollEvent() {
    SDL_Event evt;

    int mousex = 0;
    float mousexscale = 0;
    int mousey = 0;
    float mouseyscale = 0;
    int mousepressure = 0;
    while (SDL_PollEvent(&evt))
    {
        switch (evt.type) {
            case SDL_WINDOWEVENT:
            int h, w;
            SDL_GL_GetDrawableSize(win,&w,&h);
                switch (evt.window.event) {
					case SDL_WINDOWEVENT_RESIZED:
						resize(w, h);
						break;
					case SDL_WINDOWEVENT_SIZE_CHANGED:
                        resize(w, h);
						break;
                }
                break;
            case SDL_MOUSEWHEEL:
                scrollHandler(&evt);
            case SDL_KEYDOWN:
                keyHandler(&evt);
                break;
            case SDL_MOUSEBUTTONDOWN:
                if (evt.button.button == SDL_BUTTON_LEFT) {
                    // if it's the first mouse down event (since mouse up or since SDL was launched)
                    if (!mouseDown) {
                        // Get mouse coorindates when you click.
                        SDL_GetMouseState(&mousex, &mousey);
                        // Scale those coordinates. libProjectM supports a scale of 0.1 instead of absolute pixel coordinates.
                        mousexscale = (mousex / (float)width);
                        mouseyscale = ((height - mousey) / (float)height);
                        // Touch. By not supplying a touch type, we will default to random.
                        touch(mousexscale, mouseyscale, mousepressure);
                        mouseDown = true;
                    }
                }
                else if (evt.button.button == SDL_BUTTON_RIGHT)
                {
                    mouseDown = false;

                    // Keymod = Left or Right Gui or Left Ctrl. This is a shortcut to remove all waveforms.
                    if (keymod) {
                        touchDestroyAll();
                        keymod = false;
                        break;
                    }

                    // Right Click
                    SDL_GetMouseState(&mousex, &mousey);

                    // Scale those coordinates. libProjectM supports a scale of 0.1 instead of absolute pixel coordinates.
                    mousexscale = (mousex / (float)width);
                    mouseyscale = ((height - mousey) / (float)height);

                    // Destroy at the coordinates we clicked.
                    touchDestroy(mousexscale, mouseyscale);
                }
                break;
            case SDL_MOUSEBUTTONUP:
                mouseDown = false;
                break;
            case SDL_TEXTINPUT:
                if (projectM::isTextInputActive(true))
                {
                    projectM::setSearchText(evt.text.text);
                    projectM::populatePresetMenu();
                }
                break;
            case SDL_QUIT:
                done = true;
                break;
        }
    }

    // Handle dragging your waveform when mouse is down.
    if (mouseDown) {
        // Get mouse coordinates when you click.
        SDL_GetMouseState(&mousex, &mousey);
        // Scale those coordinates. libProjectM supports a scale of 0.1 instead of absolute pixel coordinates.
        mousexscale = (mousex / (float)width);
        mouseyscale = ((height - mousey) / (float)height);
        // Drag Touch.
        touchDrag(mousexscale, mouseyscale, mousepressure);
    }
}

// This touches the screen to generate a waveform at X / Y.
void projectMSDL::touch(float x, float y, int pressure, int touchtype) {
#ifdef PROJECTM_TOUCH_ENABLED
    projectM::touch(x, y, pressure, touchtype);
#endif
}

// This moves the X Y of your existing waveform that was generated by a touch (only if you held down your click and dragged your mouse around).
void projectMSDL::touchDrag(float x, float y, int pressure) {
    projectM::touchDrag(x, y, pressure);
}

// Remove waveform at X Y
void projectMSDL::touchDestroy(float x, float y) {
    projectM::touchDestroy(x, y);
}

// Remove all waveforms
void projectMSDL::touchDestroyAll() {
    projectM::touchDestroyAll();
}

void projectMSDL::renderFrame() {
    glClearColor( 0.0, 0.0, 0.0, 0.0 );
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    projectM::renderFrame();

    if (renderToTexture) {
        renderTexture();
    }

    SDL_GL_SwapWindow(win);
}

projectMSDL::projectMSDL(Settings settings, int flags) : projectM(settings, flags) {
    width = getWindowWidth();
    height = getWindowHeight();
    done = 0;
    isFullScreen = false;
}

projectMSDL::projectMSDL(std::string config_file, int flags) : projectM(config_file, flags) {
    width = getWindowWidth();
    height = getWindowHeight();
    done = 0;
    isFullScreen = false;
}

void projectMSDL::init(SDL_Window *window, SDL_GLContext *_glCtx, const bool _renderToTexture) {
    win = window;
    glCtx = _glCtx;
    projectM_resetGL(width, height);

#ifdef WASAPI_LOOPBACK
    wasapi = true;
#endif

    // are we rendering to a texture?
    renderToTexture = _renderToTexture;
    if (renderToTexture) {
        programID = ShaderEngine::CompileShaderProgram(
            StaticGlShaders::Get()->GetV2fC4fT2fVertexShader(),
            StaticGlShaders::Get()->GetV2fC4fT2fFragmentShader(), "v2f_c4f_t2f");
        textureID = projectM::initRenderToTexture();

        float points[16] = {
            -0.8, -0.8,
            0.0,    0.0,

            -0.8, 0.8,
            0.0,   1.0,

            0.8, -0.8,
            1.0,    0.0,

            0.8, 0.8,
            1.0,    1.0,
        };

        glGenBuffers(1, &m_vbo);
        glBindBuffer(GL_ARRAY_BUFFER, m_vbo);
        glBufferData(GL_ARRAY_BUFFER, sizeof(float) * 16, points, GL_DYNAMIC_DRAW);

        glGenVertexArrays(1, &m_vao);
        glBindVertexArray(m_vao);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)0); // Positions

        glDisableVertexAttribArray(1);

        glEnableVertexAttribArray(2);
        glVertexAttribPointer(2, 2, GL_FLOAT, GL_FALSE, sizeof(float)*4, (void*)(sizeof(float)*2)); // Textures

        glBindVertexArray(0);
        glBindBuffer(GL_ARRAY_BUFFER, 0);
    }
}


std::string projectMSDL::getActivePresetName()
{
    unsigned int index = 0;
    if (selectedPresetIndex(index)) {
        return getPresetName(index);
    }
    return std::string();
}


void projectMSDL::renderTexture() {
    static int frame = 0;
    frame++;

    glClear(GL_COLOR_BUFFER_BIT);
    glClear(GL_DEPTH_BUFFER_BIT);

    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glViewport(0, 0, getWindowWidth(), getWindowHeight());

    glUseProgram(programID);

    glUniform1i(glGetUniformLocation(programID, "texture_sampler"), 0);

    glm::mat4 mat_proj = glm::frustum(-1.0f, 1.0f, -1.0f, 1.0f, 2.0f, 10.0f);

    glEnable(GL_DEPTH_TEST);

    glm::mat4 mat_model = glm::mat4(1.0f);
    mat_model = glm::translate(mat_model, glm::vec3(cos(frame*0.023),
                                                    cos(frame*0.017),
                                                    -5+sin(frame*0.022)*2));
    mat_model = glm::rotate(mat_model, glm::radians((float) sin(frame*0.0043)*360),
                            glm::vec3(sin(frame*0.0017),
                                      sin(frame *0.0032),
                                      1));

    glm::mat4 mat_transf = glm::mat4(1.0f);
    mat_transf = mat_proj * mat_model;

    glUniformMatrix4fv(glGetUniformLocation(programID, "vertex_transformation"), 1, GL_FALSE, glm::value_ptr(mat_transf));

    glActiveTexture(GL_TEXTURE0);

    glBindTexture(GL_TEXTURE_2D, textureID);

    glVertexAttrib4f(1, 1.0, 1.0, 1.0, 1.0);

    glBindVertexArray(m_vao);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glBindVertexArray(0);

    glDisable(GL_DEPTH_TEST);
}

void projectMSDL::presetSwitchedEvent(bool isHardCut, size_t index) const {
    std::string presetName = getPresetName(index);
    SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, "Displaying preset: %s\n", presetName.c_str());

    std::string newTitle = "projectM ➫ " + presetName;
    SDL_SetWindowTitle(win, newTitle.c_str());
}
