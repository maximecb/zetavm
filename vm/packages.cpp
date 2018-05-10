#include <cassert>
#include <ctime>
#include <iostream>
#include <regex>
#include <unordered_map>
#include "util.h"
#include "packages.h"
#include "parser.h"
#include "serialize.h"
#include "interp.h"

#ifdef HAVE_SDL2
#include <SDL.h>
#endif

HostFn::HostFn(std::string name, size_t numParams, void* fptr)
: name(name),
  numParams(numParams),
  fptr(fptr)
{
}

Value HostFn::call0()
{
    assert (fptr);
    assert (numParams == 0);
    auto f0 = (Value(*)()) fptr;
    return f0();
}

Value HostFn::call1(Value arg0)
{
    assert (fptr);
    assert (numParams == 1);
    auto f1 = (Value(*)(Value)) fptr;
    return f1(arg0);
}

Value HostFn::call2(Value arg0, Value arg1)
{
    assert (fptr);
    assert (numParams == 2);
    auto f2 = (Value(*)(Value,Value)) fptr;
    return f2(arg0, arg1);
}

Value HostFn::call3(Value arg0, Value arg1, Value arg2)
{
    assert (fptr);
    assert (numParams == 3);
    auto f3 = (Value(*)(Value,Value,Value)) fptr;
    return f3(arg0, arg1, arg2);
}

void setHostFn(
    Object pkgObj,
    std::string name,
    size_t numParams,
    void* fptr
)
{
    auto fnObj = new HostFn(name, numParams, fptr);

    auto fnVal = Value((refptr)fnObj, TAG_HOSTFN);

    assert (!pkgObj.hasField(name));

    pkgObj.setField(name, fnVal);
}

//============================================================================
// core/vm/0 package
//============================================================================

namespace core_vm_0
{
    /**
    Import a package, initialize it and cache the initialized package
    */
    Value import(Value pkgName)
    {
        if (!pkgName.isString())
            throw RunError("import expects package name to be a string");

        return ::import(std::string(pkgName));
    }

    /**
    Load a ZIM file into memory, but do not run any initialization code
    */
    Value load(Value pkgName)
    {
        if (!pkgName.isString())
            throw RunError("load expects package name to be a string");

        return parseFile(std::string(pkgName));
    }

    /**
    Parse a string in Zeta image format (ZIM)
    */
    Value parse(Value strVal)
    {
        if (!strVal.isString())
            throw RunError("parse function expects string input");

        std::string str = (std::string)strVal;
        return parseString(str, "string");
    }

    /**
    Serialize data into Zeta image format (ZIM), returns a string
    */
    Value serialize(Value val, Value indent)
    {
        bool indentBool = (indent == Value::TRUE);
        std::string str = ::serialize(val, indentBool);
        return String(str);
    }

    /**
    Get the number of garbage collections performed so far.
    */
    Value get_gc_count()
    {
        assert (false && "implement me");
    }

    /**
    Manually trigger a garbage collection. Used for testing.
    */
    Value gc_collect()
    {
        assert (false && "implement me");
        return Value::UNDEF;
    }

    Value get_pkg()
    {
        auto exports = Object(32);
        setHostFn(exports, "import"       , 1, (void*)import);
        setHostFn(exports, "load"         , 1, (void*)load);
        setHostFn(exports, "parse"        , 1, (void*)parse);
        setHostFn(exports, "serialize"    , 2, (void*)serialize);
        setHostFn(exports, "get_gc_count" , 0, (void*)get_gc_count);
        setHostFn(exports, "gc_collect"   , 0, (void*)gc_collect);
        return exports;
    }
};

HostFn importFn("import", 1, (void*)core_vm_0::import);

//============================================================================
// core/io/0 package
//============================================================================

namespace core_io_0
{
    Value print_int32(Value val)
    {
        assert (val.isInt32());
        std::cout << (int32_t)val;
        return Value::UNDEF;
    }

    Value print_float32(Value val)
    {
        assert (val.isFloat32());
        std::cout << (float)val;
        return Value::UNDEF;
    }

    Value print_str(Value val)
    {
        assert (val.isString());
        std::cout << (std::string)val;
        return Value::UNDEF;
    }

    Value read_file(Value fileName)
    {
        assert (fileName.isString());
        auto nameStr = (std::string)fileName;

        FILE* file = fopen(nameStr.c_str(), "r");

        if (!file)
        {
            throw RunError("failed to open file \"" + nameStr + "\"");
        }

        // Get the file size in bytes
        fseek(file, 0, SEEK_END);
        size_t len = ftell(file);
        fseek(file, 0, SEEK_SET);

        char* buf = (char*)malloc(len+1);

        // Read into the allocated buffer
        size_t read = fread(buf, 1, len, file);

        if (read != len)
        {
            throw RunError("failed to read file \"" + nameStr + "\"");
        }

        // Add a null terminator to the string
        buf[len] = '\0';

        // Close the input file
        fclose(file);

        return String(buf);
    }

    Value write_file(Value fileName, Value data)
    {
        assert (fileName.isString());
        assert (data.isString());

        auto nameStr = (std::string)fileName;

        FILE* file = fopen(nameStr.c_str(), "w");

        if (!file)
        {
            throw RunError("failed to open file \"" + nameStr + "\"");
        }

        auto dataStr = String(data);

        fwrite(
            dataStr.getDataPtr(),
            dataStr.length(),
            1,
            file
        );

        fclose(file);

        return Value::TRUE;
    }

    Value read_line()
    {
        char* lineBuf = nullptr;
        size_t bufSize = 0;

        auto numRead = getdelim(&lineBuf, &bufSize, '\n', stdin);

        if (numRead <= 0)
        {
            free(lineBuf);
            return Value::UNDEF;
        }

        // Clear trailing end of line characters
        for (ssize_t i = 0; i < numRead; ++i)
        {
            size_t idx = numRead - 1 - i;

            if (lineBuf[idx] == '\n' || lineBuf[idx] == '\r')
            {
                lineBuf[idx] = '\0';
            }
            else
            {
                break;
            }
        }

        auto str = String(lineBuf);
        free(lineBuf);

        return str;
    }

    Value get_pkg()
    {
        auto exports = Object(32);
        setHostFn(exports, "print_int32"  , 1, (void*)print_int32);
        setHostFn(exports, "print_float32", 1, (void*)print_float32);
        setHostFn(exports, "print_str"    , 1, (void*)print_str);
        setHostFn(exports, "read_file"    , 1, (void*)read_file);
        setHostFn(exports, "write_file"   , 2, (void*)write_file);
        setHostFn(exports, "read_line"    , 0, (void*)read_line);
        return exports;
    }
}

//============================================================================
// core/time/0 package
//============================================================================

#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/time.h>
#endif

namespace core_time_0
{
    // Time when the time package was loaded
    #ifdef _WIN32
        LARGE_INTEGER startTime;
    #else
        timeval startTime;
    #endif

    /**
    Get the time in milliseconds since the time library was loaded
    The value produced is of int32 type
    */
    Value get_time_millis()
    {
        #ifdef _WIN32
            LARGE_INTEGER frequency;
            QueryPerformanceFrequency(&frequency);
            LARGE_INTEGER time;
            QueryPerformanceCounter(&time);
            LARGE_INTEGER delta = time.QuadPart - startTime.QuadPart;
            int64_t millis = (int64_t)(1000 * (delta / frequency.QuadPart));
            return Value::int32((int32_t)millis);
        #else
            timeval time;
            gettimeofday(&time, NULL);
            int64_t delta = 0;
            delta += (time.tv_sec - startTime.tv_sec) * 1000;
            delta += (time.tv_usec - startTime.tv_usec) / 1000.0;
            return Value::int32((int32_t)delta);
        #endif
    }

    /**
     * Populates an object with the fields of a <time.h> `tm` struct. A few
     * fields have been altered to make their values more useful:
     *
     * - The year is no longer relative to 1900, we simply add 1900 to the value.
     *
     * - Per time.h's specifications, isdst is positive when true, zero when
     *   false and negative when inconclusive. To model this in plush, we return
     *   true/false when the answer is known and undefined when it is not.
     */
    Value get_local_time()
    {
        time_t rawtime;
        struct tm* timeinfo;

        time(&rawtime);
        timeinfo = localtime(&rawtime);

        auto obj = Object();
        obj.setField("sec", Value::int32(timeinfo->tm_sec));
        obj.setField("min", Value::int32(timeinfo->tm_min));
        obj.setField("hour", Value::int32(timeinfo->tm_hour));
        obj.setField("day", Value::int32(timeinfo->tm_mday));
        obj.setField("month", Value::int32(timeinfo->tm_mon));
        obj.setField("year", Value::int32(timeinfo->tm_year + 1900));
        obj.setField("week_day", Value::int32(timeinfo->tm_wday));
        obj.setField("year_day", Value::int32(timeinfo->tm_yday));

        const auto& isdst = timeinfo->tm_isdst;
        if (isdst > 0)
          obj.setField("is_dst", Value::TRUE);
        else if (isdst == 0)
          obj.setField("is_dst", Value::FALSE);
        else
          obj.setField("is_dst", Value::UNDEF);

        return obj;
    }

    Value get_pkg()
    {
        // Store the time value when the time package is loaded
        #ifdef _WIN32
            QueryPerformanceCounter(&startTime);
        #else
            gettimeofday(&startTime, NULL);
        #endif

        auto exports = Object(32);
        setHostFn(exports, "get_time_millis", 0, (void*)get_time_millis);
        setHostFn(exports, "get_local_time", 0, (void*)get_local_time);
        return exports;
    }
}

//============================================================================
// core/window/0 package
//============================================================================

namespace core_window_0
{
#ifdef HAVE_SDL2
    size_t width = 0;
    size_t height = 0;

    std::vector<uint32_t> pixelBuffer;

    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;

    /**
    Convert an SDL keycode to a key name
    Note: we intentionally avoid using the SDL function for doing this
          to hide the underlying implementation
    */
    std::string sdlKeyToStr(int keyCode)
    {
        // Note: platform-specific keys such as mac command
        // and windows keys are intentionally not listed here,
        // to encourage portable application development

        if (keyCode == SDLK_LEFT)
            return "left";
        if (keyCode == SDLK_RIGHT)
            return "right";
        if (keyCode == SDLK_UP)
            return "up";
        if (keyCode == SDLK_DOWN)
            return "down";

        if (keyCode == SDLK_SPACE)
            return "space";
        if (keyCode == SDLK_RETURN)
            return "return";
        if (keyCode == SDLK_DELETE)
            return "delete";
        if (keyCode == SDLK_ESCAPE)
            return "escape";

        // Note: not distinguishable on every system, so we
        // alias left/right control and shift
        if (keyCode == SDLK_LCTRL || keyCode == SDLK_RCTRL)
            return "ctrl";
        if (keyCode == SDLK_LSHIFT || keyCode == SDLK_RSHIFT)
            return "shift";

        if (keyCode >= SDLK_a && keyCode <= SDLK_z)
        {
            auto idx = keyCode - SDLK_a;
            std::string keyName;
            keyName = 'a' + idx;
            return keyName;
        }

        if (keyCode >= SDLK_0 && keyCode <= SDLK_9)
        {
            auto idx = keyCode - SDLK_0;
            std::string keyName;
            keyName = '0' + idx;
            return keyName;
        }

        if (keyCode >= SDLK_F1 && keyCode <= SDLK_F12)
        {
            auto num = keyCode - SDLK_F1 + 1;
            return "f" + std::to_string(num);
        }

        // Unknown key
        return "";
    }

    Value create_window(
        Value titleVal,
        Value widthVal,
        Value heightVal
    )
    {
        SDL_Init(SDL_INIT_VIDEO);

        auto title = (std::string)titleVal;
        width = (size_t)(int32_t)widthVal;
        height = (size_t)(int32_t)heightVal;

        window = SDL_CreateWindow(
            title.c_str(),
            SDL_WINDOWPOS_UNDEFINED,
            SDL_WINDOWPOS_UNDEFINED,
            width,
            height,
            0
        );

        renderer = SDL_CreateRenderer(window, -1, 0);

        texture = SDL_CreateTexture(
            renderer,
            SDL_PIXELFORMAT_RGBA8888,
            SDL_TEXTUREACCESS_STATIC,
            width,
            height
        );

        pixelBuffer.resize(width * height, 0);

        // Clear the window
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);

        SDL_ShowWindow(window);

        // Return the window handle, hidden in a raw pointer value
        return Value((refptr)window, TAG_RAWPTR);
    }

    Value destroy_window(Value handle)
    {
        // For now, only one window is supported
        assert (handle == Value((refptr)window, TAG_RAWPTR));

        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();

        pixelBuffer.clear();

        return Value::UNDEF;
    }

    /**
    Get the next event to process, if any
    */
    Value get_next_event(Value handle)
    {
        // For now, only one window is supported
        assert (handle == Value((refptr)window, TAG_RAWPTR));

        SDL_Event event;

        // While there are events to process
        while (SDL_PollEvent(&event))
        {
            switch (event.type)
            {
                case SDL_QUIT:
                {
                    auto obj = Object();
                    obj.setField("type", String("quit"));
                    return obj;
                }

                case SDL_KEYDOWN:
                case SDL_KEYUP:
                {
                    auto eventType = String(
                        event.type == SDL_KEYDOWN?
                        "key_down":"key_up"
                    );

                    std::string keyName = sdlKeyToStr(event.key.keysym.sym);

                    // Unknown key, ignore this event
                    if (keyName == "")
                        continue;

                    auto obj = Object();
                    obj.setField("type", eventType);
                    obj.setField("key", String(keyName));
                    return obj;
                }
                break;

                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                {
                    auto eventType = String(
                        event.type == SDL_MOUSEBUTTONDOWN?
                        "mouse_down":"mouse_up"
                    );

                    int32_t buttonIdx;
                    switch (event.button.button)
                    {
                        case SDL_BUTTON_LEFT: buttonIdx = 0; break;
                        case SDL_BUTTON_MIDDLE: buttonIdx = 1; break;
                        case SDL_BUTTON_RIGHT: buttonIdx = 2; break;
                        case SDL_BUTTON_X1: buttonIdx = 3; break;
                        case SDL_BUTTON_X2: buttonIdx = 4; break;
                        default: assert (false);
                    }

                    auto obj = Object();
                    obj.setField("type", eventType);
                    obj.setField("x", Value::int32(event.button.x));
                    obj.setField("y", Value::int32(event.button.y));
                    obj.setField("button", Value::int32(buttonIdx));
                    return obj;
                }

                // Process the next event
                default:
                continue;
            }
        }

        // No event to process
        return Value::FALSE;
    }

    /**
    Display a bitmap (array of pixels) into the window.
    Pixels are in ABGR format (alpha least significant),
    with one int32 value per pixel.
    */
    Value draw_bitmap(Value handle, Value pixelsArray)
    {
        // For now, only one window is supported
        assert (handle == Value((refptr)window, TAG_RAWPTR));

        auto pixels = (Array)pixelsArray;
        assert (pixels.length() == width * height);

        for (size_t pixIdx = 0; pixIdx < width * height; ++pixIdx)
        {
            // Mask out the alpha channel so its content is ignored
            auto pixVal = (int32_t)pixels.getElem(pixIdx);
            pixVal = 0xFFFFFF00 & pixVal;
            pixelBuffer[pixIdx] = pixVal;
        }

        SDL_UpdateTexture(texture, NULL, &pixelBuffer[0], width * 4);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderCopy(renderer, texture, NULL, NULL);
        SDL_RenderPresent(renderer);
        return Value::UNDEF;
    }
#endif // HAVE_SDL2

    Value get_pkg()
    {
#ifdef HAVE_SDL2
        auto exports = Object(32);
        setHostFn(exports, "create_window"  , 3, (void*)create_window);
        setHostFn(exports, "destroy_window" , 1, (void*)destroy_window);
        setHostFn(exports, "get_next_event" , 1, (void*)get_next_event);
        setHostFn(exports, "draw_bitmap"    , 2, (void*)draw_bitmap);
        return exports;
#else
        throw DepMissing("core/window", "libsdl2", "--with-sdl2");
#endif
    }
}

//============================================================================
// core/audio/0 package
//============================================================================

namespace core_audio_0
{
#ifdef HAVE_SDL2
    bool paused = true;

    Value open_output_device(
        Value sample_rate_val,
        Value num_channels
    )
    {
        assert (sample_rate_val.isInt32());
        assert (num_channels.isInt32());

        auto sampleRate = (int32_t)sample_rate_val;
        if (sampleRate != 44100)
            throw RunError("sample rate is currently fixed to 44100");

        SDL_Init(SDL_INIT_AUDIO);
        SDL_AudioSpec want, have;

        SDL_zero(want);
        want.freq = sampleRate;
        want.format = AUDIO_F32;
        want.channels = (uint8_t)(int32_t)num_channels;
        want.samples = 4096;
        want.callback = NULL;

        auto devId = (int32_t)SDL_OpenAudioDevice(NULL, 0, &want, &have, SDL_AUDIO_ALLOW_ANY_CHANGE);

        assert (want.format == have.format);
        assert (want.freq == have.freq);
        assert (want.channels = have.channels);

        return Value::int32(devId);
    }

    Value queue_samples(
        Value dev,
        Value samplesArray
    )
    {
        assert(dev.isInt32());
        assert(samplesArray.isArray());
        auto devID = (int32_t)dev;

        if (paused)
        {
            paused = false;
            SDL_PauseAudioDevice(devID, 0);
        }

        auto samples = (Array)samplesArray;

        if (samples.length() == 0)
            return Value::UNDEF;

        float samples_buf[samples.length()];

        for (size_t i = 0; i < samples.length(); i++)
        {
            auto elem = samples.getElem(i);

            if (!elem.isFloat32())
            {
                throw RunError("audio samples must be float32");
            }

            float sample = (float)elem > 1.0f ? 1.0f : elem;
            sample = sample < -1.0f ? -1.0f : sample;
            samples_buf[i] = sample;
        }

        if (SDL_QueueAudio(devID, samples_buf, sizeof(samples_buf)) != 0)
        {
            return Value::FALSE;
        }

        return Value::TRUE;
    }

    Value get_queue_size(
        Value dev
    )
    {
        assert(dev.isInt32());

        auto devID = (int32_t)dev;

        auto numBytes = SDL_GetQueuedAudioSize(devID);
        auto numSamples = numBytes / 4;

        return Value::int32((int32_t)numSamples);
    }

    Value close_output_device(
        Value dev
    )
    {
        assert(dev.isInt32());

        auto devID = (int32_t)dev;
        SDL_CloseAudioDevice(devID);

        return Value::UNDEF;
    }
#endif // HAVE_SDL2

    Value get_pkg()
    {
#ifdef HAVE_SDL2
        auto exports = Object(32);
        setHostFn(exports, "open_output_device" , 2, (void*)open_output_device);
        setHostFn(exports, "close_output_device", 1, (void*)close_output_device);
        setHostFn(exports, "queue_samples"      , 2, (void*)queue_samples);
        setHostFn(exports, "get_queue_size"     , 1, (void*)get_queue_size);
        return exports;
#else
        throw DepMissing("core/audio", "libsdl2", "--with-sdl2");
#endif
    }
}

//============================================================================

// Cache of loaded packages
std::unordered_map<std::string, Value> pkgCache;

/// Load a package based on its path
Object load(std::string pkgPath)
{
    Input input(pkgPath);

    Value exportVal;

    // Parse the language directive
    auto langPkgName = parseLang(input);

    // If a language package is specified
    if (langPkgName != "")
    {
        //std::cout << "Loading language package" << std::endl;

        auto langPkg = import(langPkgName);

        if (!langPkg.hasField("parse_input"))
        {
            throw RunError(
                "parser packages must export a parse_input function"
            );
        }

        // Create an object to pass the input data
        auto inputObj = Object();
        inputObj.setField("src_name", String(input.getSrcName()));
        inputObj.setField("src_string", String(input.getInputStr()));
        inputObj.setField("str_idx", Value::int32(input.getInputIdx()));
        inputObj.setField("line_no", Value::int32(input.getLineNo()));
        inputObj.setField("col_no", Value::int32(input.getColNo()));

        //std::cout << "Calling parse_input" << std::endl;

        // Call the parse_input method exported by the parser package
        ValueVec args;
        args.push_back(inputObj);
        exportVal = callExportFn(langPkg, "parse_input", args);

        //std::cout << "Returned from parse_input" << std::endl;
    }
    else
    {
        // Parse the package file contents
        exportVal = parseInput(input);
    }

    if (!exportVal.isObject())
    {
        throw RunError("exports value is not an object");
    }

    auto pkg = Object(exportVal);

    return pkg;
}

Value getCorePkg(std::string pkgName)
{
    // Internal/core packages
    if (pkgName == "core/vm/0")
        return core_vm_0::get_pkg();
    if (pkgName == "core/io/0")
        return core_io_0::get_pkg();
    if (pkgName == "core/time/0")
        return core_time_0::get_pkg();
    if (pkgName == "core/window/0")
        return core_window_0::get_pkg();
    if (pkgName == "core/audio/0")
        return core_audio_0::get_pkg();

    return Value::UNDEF;
}

/// Import a package based on its name, and perform caching
Object import(std::string pkgName)
{
    // If the package is already loaded
    auto itr = pkgCache.find(pkgName);
    if (itr != pkgCache.end())
    {
        return Object(itr->second);
    }

    std::string pkgPath = "";

    // If this is a local import
    if (pkgName.substr(0, 2) == "./")
    {
        if (!fileExists(pkgName))
        {
            throw ImportError("local package not found \"" + pkgName + "\"");
        }

        pkgPath = pkgName;
    }

    // Otherwise, this is a global import
    else
    {
        if (!regex_match(pkgName, std::regex("([a-z0-9]+/)*[0-9]+")))
        {
            throw ImportError("invalid global package name \"" + pkgName + "\"");
        }

        // Look in the package directory
        auto pkgDirPath = PKGS_DIR + pkgName + "/package";

        if (fileExists(pkgDirPath))
        {
            pkgPath = pkgDirPath;
        }
    }

    // If a package file was found for the given package name
    if (pkgPath != "")
    {
        // Load the package file
        auto pkg = load(pkgPath);

        // Cache the package
        pkgCache[pkgName] = pkg;

        // Initialize the package
        if (pkg.hasField("init"))
        {
            callExportFn(pkg, "init");
        }

        return pkg;
    }

    // If we can find a core package for this name
    auto corePkg = getCorePkg(pkgName);
    if (corePkg != Value::UNDEF)
    {
        pkgCache[pkgName] = corePkg;
        return Object(corePkg);
    }

    // Package not found
    throw ImportError("global package not found \"" + pkgName + "\"");
}
