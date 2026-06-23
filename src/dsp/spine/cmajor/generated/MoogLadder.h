
#include <cstdint>
#include <cmath>
#include <string>
#include <cstring>
#include <array>
#include <stdexcept>

//==============================================================================
/// Auto-generated C++ class for the 'MoogLadder' processor
///

#if ! (defined (__cplusplus) && (__cplusplus >= 201703L))
 #error "This code requires that your compiler is set to use C++17 or later!"
#endif

struct MoogLadder
{
    MoogLadder() = default;
    ~MoogLadder() = default;

    static constexpr std::string_view name = "MoogLadder";

    //==============================================================================
    using EndpointHandle = uint32_t;

    enum class EndpointType
    {
        stream,
        event,
        value
    };

    struct EndpointInfo
    {
        uint32_t handle;
        const char* name;
        EndpointType endpointType;
    };

    //==============================================================================
    static constexpr uint32_t numInputEndpoints  = 6;
    static constexpr uint32_t numOutputEndpoints = 1;

    static constexpr uint32_t maxFramesPerBlock  = 32;
    static constexpr uint32_t eventBufferSize    = 32;
    static constexpr uint32_t maxOutputEventSize = 0;
    static constexpr double   latency            = 0.000000;

    enum class EndpointHandles
    {
        in        = 1,
        out       = 7,
        cutoffHz  = 2,
        resonance = 3,
        drive     = 4,
        slope     = 5,
        mode      = 6
    };

    static constexpr uint32_t getEndpointHandleForName (std::string_view endpointName)
    {
        if (endpointName == "in")         return static_cast<uint32_t> (EndpointHandles::in);
        if (endpointName == "out")        return static_cast<uint32_t> (EndpointHandles::out);
        if (endpointName == "cutoffHz")   return static_cast<uint32_t> (EndpointHandles::cutoffHz);
        if (endpointName == "resonance")  return static_cast<uint32_t> (EndpointHandles::resonance);
        if (endpointName == "drive")      return static_cast<uint32_t> (EndpointHandles::drive);
        if (endpointName == "slope")      return static_cast<uint32_t> (EndpointHandles::slope);
        if (endpointName == "mode")       return static_cast<uint32_t> (EndpointHandles::mode);
        return 0;
    }

    static constexpr EndpointInfo inputEndpoints[] =
    {
        { 1,  "in",         EndpointType::stream },
        { 2,  "cutoffHz",   EndpointType::event  },
        { 3,  "resonance",  EndpointType::event  },
        { 4,  "drive",      EndpointType::event  },
        { 5,  "slope",      EndpointType::event  },
        { 6,  "mode",       EndpointType::event  }
    };

    static constexpr EndpointInfo outputEndpoints[] =
    {
        { 7,  "out",  EndpointType::stream }
    };

    //==============================================================================
    static constexpr uint32_t numAudioInputChannels  = 1;
    static constexpr uint32_t numAudioOutputChannels = 1;

    static constexpr std::array outputAudioStreams
    {
        outputEndpoints[0]
    };

    static constexpr std::array<EndpointInfo, 0> outputEvents {};

    static constexpr std::array<EndpointInfo, 0> outputMIDIEvents {};

    static constexpr std::array inputAudioStreams
    {
        inputEndpoints[0]
    };

    static constexpr std::array inputEvents
    {
        inputEndpoints[1],
        inputEndpoints[2],
        inputEndpoints[3],
        inputEndpoints[4],
        inputEndpoints[5]
    };

    static constexpr std::array<EndpointInfo, 0> inputMIDIEvents {};

    static constexpr std::array inputParameters
    {
        inputEndpoints[1],
        inputEndpoints[2],
        inputEndpoints[3],
        inputEndpoints[4],
        inputEndpoints[5]
    };

    static constexpr const char* programDetailsJSON =
            "{\n"
            "  \"mainProcessor\": \"MoogLadder\",\n"
            "  \"inputs\": [\n"
            "    {\n"
            "      \"endpointID\": \"in\",\n"
            "      \"endpointType\": \"stream\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"purpose\": \"audio in\",\n"
            "      \"numAudioChannels\": 1\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"cutoffHz\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Cutoff\",\n"
            "        \"min\": 16,\n"
            "        \"max\": 20000,\n"
            "        \"init\": 1000\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"resonance\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Resonance\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 1,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"drive\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Drive\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 1,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"slope\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"int32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Slope\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 1,\n"
            "        \"init\": 1\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"mode\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"int32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Mode\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 2,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    }\n"
            "  ],\n"
            "  \"outputs\": [\n"
            "    {\n"
            "      \"endpointID\": \"out\",\n"
            "      \"endpointType\": \"stream\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"purpose\": \"audio out\",\n"
            "      \"numAudioChannels\": 1\n"
            "    }\n"
            "  ]\n"
            "}";

    //==============================================================================
    struct intrinsics;

    using SizeType = int32_t;
    using IndexType = int32_t;
    using StringHandle = uint32_t;

    struct Null
    {
        template <typename AnyType> operator AnyType() const    { return {}; }
        Null operator[] (IndexType) const                       { return {}; }
    };

    //==============================================================================
    template <typename ElementType, SizeType numElements>
    struct Array
    {
        Array() = default;
        Array (Null) {}
        Array (const Array&) = default;

        template <typename ElementOrList>
        Array (const ElementOrList& value) noexcept
        {
            if constexpr (std::is_convertible<ElementOrList, ElementType>::value)
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value);
            }
            else
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value[i]);
            }
        }

        template <typename... Others>
        Array (ElementType e0, ElementType e1, Others... others) noexcept
        {
            this->elements[0] = static_cast<ElementType> (e0);
            this->elements[1] = static_cast<ElementType> (e1);

            if constexpr (numElements > 2)
            {
                const ElementType initialisers[] = { static_cast<ElementType> (others)... };

                for (size_t i = 0; i < sizeof...(others); ++i)
                    this->elements[i + 2] = initialisers[i];
            }
        }

        Array (const ElementType* rawArray, size_t) noexcept
        {
            for (IndexType i = 0; i < numElements; ++i)
                this->elements[i] = rawArray[i];
        }

        Array& operator= (const Array&) noexcept = default;
        Array& operator= (Null) noexcept                 { this->clear(); return *this; }

        template <typename ElementOrList>
        Array& operator= (const ElementOrList& value) noexcept
        {
            if constexpr (std::is_convertible<ElementOrList, ElementType>::value)
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value);
            }
            else
            {
                for (IndexType i = 0; i < numElements; ++i)
                    this->elements[i] = static_cast<ElementType> (value[i]);
            }
        }

        static constexpr SizeType size()                                    { return numElements; }

        const ElementType& operator[] (IndexType index) const noexcept      { return this->elements[index]; }
        ElementType& operator[] (IndexType index) noexcept                  { return this->elements[index]; }

        void clear() noexcept
        {
            for (auto& element : elements)
                element = ElementType();
        }

        void clear (SizeType numElementsToClear) noexcept
        {
            for (SizeType i = 0; i < numElementsToClear; ++i)
                elements[i] = ElementType();
        }

        ElementType elements[numElements] = {};
    };

    //==============================================================================
    template <typename ElementType, SizeType numElements>
    struct Vector  : public Array<ElementType, numElements>
    {
        Vector() = default;
        Vector (Null) {}

        template <typename ElementOrList>
        Vector (const ElementOrList& value) noexcept  : Array<ElementType, numElements> (value) {}

        template <typename... Others>
        Vector (ElementType e0, ElementType e1, Others... others) noexcept  : Array<ElementType, numElements> (e0, e1, others...) {}

        Vector (const ElementType* rawArray, size_t) noexcept  : Array<ElementType, numElements> (rawArray, size_t()) {}

        template <typename ElementOrList>
        Vector& operator= (const ElementOrList& value) noexcept { return Array<ElementType, numElements>::operator= (value); }

        Vector& operator= (Null) noexcept { this->clear(); return *this; }

        operator ElementType() const noexcept
        {
            static_assert (numElements == 1);
            return this->elements[0];
        }

        constexpr auto operator!() const noexcept     { return performUnaryOp ([] (ElementType n) { return ! n; }); }
        constexpr auto operator~() const noexcept     { return performUnaryOp ([] (ElementType n) { return ~n; }); }
        constexpr auto operator-() const noexcept     { return performUnaryOp ([] (ElementType n) { return -n; }); }

        constexpr auto operator+ (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a + b; }); }
        constexpr auto operator- (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a - b; }); }
        constexpr auto operator* (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a * b; }); }
        constexpr auto operator/ (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a / b; }); }
        constexpr auto operator% (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return intrinsics::modulo (a, b); }); }
        constexpr auto operator& (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a & b; }); }
        constexpr auto operator| (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a | b; }); }
        constexpr auto operator<< (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a << b; }); }
        constexpr auto operator>> (const Vector& rhs) const noexcept   { return performBinaryOp (rhs, [] (ElementType a, ElementType b) { return a >> b; }); }

        constexpr auto operator== (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a == b; }); }
        constexpr auto operator!= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a != b; }); }
        constexpr auto operator<  (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a < b; }); }
        constexpr auto operator<= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a <= b; }); }
        constexpr auto operator>  (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a > b; }); }
        constexpr auto operator>= (const Vector& rhs) const noexcept  { return performComparison (rhs, [] (ElementType a, ElementType b) { return a >= b; }); }

        template <typename Functor>
        constexpr Vector performUnaryOp (Functor&& f) const noexcept
        {
            Vector result;

            for (IndexType i = 0; i < numElements; ++i)
                result.elements[i] = f (this->elements[i]);

            return result;
        }

        template <typename Functor>
        constexpr Vector performBinaryOp (const Vector& rhs, Functor&& f) const noexcept
        {
            Vector result;

            for (IndexType i = 0; i < numElements; ++i)
                result.elements[i] = f (this->elements[i], rhs.elements[i]);

            return result;
        }

        template <typename Functor>
        constexpr Vector<bool, numElements> performComparison (const Vector& rhs, Functor&& f) const noexcept
        {
            Vector<bool, numElements> result;

            for (IndexType i = 0; i < numElements; ++i)
                result.elements[i] = f (this->elements[i], rhs.elements[i]);

            return result;
        }
    };

    //==============================================================================
    template <typename ElementType>
    struct Slice
    {
        Slice() = default;
        Slice (Null) {}
        Slice (ElementType* e, SizeType size) : elements (e), numElements (size) {}
        Slice (const Slice&) = default;
        Slice& operator= (const Slice&) = default;
        template <typename ArrayType> Slice (const ArrayType& a) : elements (const_cast<ArrayType&> (a).elements), numElements (a.size()) {}
        template <typename ArrayType> Slice (const ArrayType& a, SizeType offset, SizeType size) : elements (const_cast<ArrayType&> (a).elements + offset), numElements (size) {}

        constexpr SizeType size() const                                     { return numElements; }
        ElementType operator[] (IndexType index) const noexcept             { return numElements == 0 ? ElementType() : elements[index]; }
        ElementType& operator[] (IndexType index) noexcept                  { return numElements == 0 ? emptyValue : elements[index]; }

        Slice slice (IndexType start, IndexType end) noexcept
        {
            if (numElements == 0) return {};
            if (start >= numElements) return {};

            return { elements + start, std::min (static_cast<SizeType> (end - start), numElements - start) };
        }

        const Slice slice (IndexType start, IndexType end) const noexcept
        {
            if (numElements == 0) return {};
            if (start >= numElements) return {};

            return { elements + start, std::min (static_cast<SizeType> (end - start), numElements - start) };
        }

        ElementType* elements = nullptr;
        SizeType numElements = 0;

        static inline ElementType emptyValue {};
    };

    //==============================================================================
    #if __clang__
     #pragma clang diagnostic push
     #pragma clang diagnostic ignored "-Wunused-variable"
     #pragma clang diagnostic ignored "-Wunused-parameter"
     #pragma clang diagnostic ignored "-Wunused-label"
     #pragma clang diagnostic ignored "-Wtautological-compare"

     #if __clang_major__ >= 14
      #pragma clang diagnostic ignored "-Wunused-but-set-variable"
     #endif

     #if __clang_major__ >= 17
      #pragma clang diagnostic ignored "-Wnan-infinity-disabled"
     #endif

    #elif __GNUC__
     #pragma GCC diagnostic push
     #pragma GCC diagnostic ignored "-Wunused-variable"
     #pragma GCC diagnostic ignored "-Wunused-parameter"
     #pragma GCC diagnostic ignored "-Wunused-but-set-variable"
     #pragma GCC diagnostic ignored "-Wunused-label"
    #else
     #pragma warning (push, 0)
     #pragma warning (disable: 4702)
     #pragma warning (disable: 4706)
    #endif

    //==============================================================================
    struct _MoogLadder_State
    {
        float g = {};
        float G = {};
        float r = {};
        float dcR = {};
        bool dirty = {};
        float cutoff = {};
        float res = {};
        float drv = {};
        int32_t slopeSel = {};
        int32_t modeSel = {};
        float s1 = {};
        float s2 = {};
        float s3 = {};
        float s4 = {};
        float yp1 = {};
        float yp2 = {};
        float yp3 = {};
        float yp4 = {};
        float dcx1 = {};
        float dcy1 = {};
        int32_t _sessionID = {};
        double _frequency = {};
        int32_t _resumeIndex = {};
    };

    struct MoogLadder_State
    {
        int32_t _currentFrame = {};
        _MoogLadder_State _state;
    };

    struct MoogLadder_IO
    {
        Array<float, 32> in;
        Array<float, 32> out;
    };

    struct _MoogLadder_IO
    {
        float in = {};
        float out = {};
    };

    using std_intrinsics_T = float;
    using std_intrinsics_T_0 = float;
    using std_intrinsics_T_1 = float;
    using std_intrinsics_T_2 = float;
    using std_intrinsics_T_3 = float;

    //==============================================================================
    double getMaxFrequency() const
    {
        return 192000.0;
    }

    void initialise (int32_t sessionID, double frequency)
    {
        if (frequency > getMaxFrequency()) throw std::runtime_error ("frequency out of range");
        initSessionID = sessionID;
        initFrequency = frequency;
        reset();
    }

    void reset()
    {
        std::memset (reinterpret_cast<char*> (&cmajState), 0, sizeof (cmajState));
        int32_t processorID = 0;
        _initialise (cmajState, processorID, initSessionID, initFrequency);
    }

    void advance (int32_t frames)
    {
        cmajIO.out.clear (static_cast<SizeType> (frames));
        _advance (cmajState, cmajIO, frames);
    }

    void copyOutputValue (EndpointHandle endpointHandle, void* dest)
    {
        (void) endpointHandle; (void) dest;

        throw std::runtime_error ("Unknown value endpointHandle:" + std::to_string (endpointHandle));
    }

    void copyOutputFrames (EndpointHandle endpointHandle, void* dest, uint32_t numFramesToCopy)
    {
        if (endpointHandle == 7) { std::memcpy (reinterpret_cast<char*> (dest), std::addressof (cmajIO.out), 4 * numFramesToCopy); std::memset (reinterpret_cast<char*> (std::addressof (cmajIO.out)), 0, 4 * numFramesToCopy); return; }
        throw std::runtime_error ("Unknown stream endpointHandle:" + std::to_string (endpointHandle));
    }

    uint32_t getNumOutputEvents (EndpointHandle endpointHandle)
    {
        (void) endpointHandle;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return {};
    }

    void resetOutputEventCount (EndpointHandle endpointHandle)
    {
        (void) endpointHandle;
    }

    uint32_t getOutputEventType (EndpointHandle endpointHandle, uint32_t index)
    {
        (void) endpointHandle; (void) index;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return {};
    }

    static uint32_t getOutputEventDataSize (EndpointHandle endpointHandle, uint32_t typeIndex)
    {
        (void) endpointHandle; (void) typeIndex;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return 0;
    }

    uint32_t readOutputEvent (EndpointHandle endpointHandle, uint32_t index, unsigned char* dest)
    {

        (void) endpointHandle; (void) index; (void) dest;

        throw std::runtime_error ("Unknown event endpointHandle:" + std::to_string (endpointHandle));
        return {};
    }

    void addEvent_cutoffHz (float event)
    {
        _sendEvent_cutoffHz (cmajState, event);
    }

    void addEvent_resonance (float event)
    {
        _sendEvent_resonance (cmajState, event);
    }

    void addEvent_drive (float event)
    {
        _sendEvent_drive (cmajState, event);
    }

    void addEvent_slope (int32_t event)
    {
        _sendEvent_slope (cmajState, event);
    }

    void addEvent_mode (int32_t event)
    {
        _sendEvent_mode (cmajState, event);
    }

    void addEvent (EndpointHandle endpointHandle, uint32_t typeIndex, const unsigned char* eventData)
    {
        (void) endpointHandle; (void) typeIndex; (void) eventData;

        if (endpointHandle == 2)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_cutoffHz (value);
        }

        if (endpointHandle == 3)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_resonance (value);
        }

        if (endpointHandle == 4)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_drive (value);
        }

        if (endpointHandle == 5)
        {
            int32_t value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_slope (value);
        }

        if (endpointHandle == 6)
        {
            int32_t value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_mode (value);
        }
    }

    void setValue (EndpointHandle endpointHandle, const void* value, int32_t frames)
    {
        (void) endpointHandle; (void) value; (void) frames;
    }

    void setInputFrames_in (const void* data, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        memcpy (cmajIO.in.elements, data, numFrames * 4);
        if (numTrailingFramesToClear != 0) memset (reinterpret_cast<char*> (cmajIO.in.elements + numFrames), 0, numTrailingFramesToClear * 4);
    }

    void setInputFrames (EndpointHandle endpointHandle, const void* frameData, uint32_t numFrames, uint32_t numTrailingFramesToClear)
    {
        if (endpointHandle == 1) return setInputFrames_in (frameData, numFrames, numTrailingFramesToClear);
    }

    //==============================================================================
    // Rendering state values
    int32_t initSessionID;
    double initFrequency;
    MoogLadder_State cmajState = {};
    MoogLadder_IO cmajIO = {};

    //==============================================================================
    void _sendEvent_cutoffHz (MoogLadder_State& _state, float value) noexcept
    {
        _MoogLadder__cutoffHz (_state._state, value);
    }

    void _MoogLadder__cutoffHz (_MoogLadder_State& _state, float v) noexcept
    {
        _state.cutoff = v;
        _state.dirty = true;
    }

    void _sendEvent_resonance (MoogLadder_State& _state, float value) noexcept
    {
        _MoogLadder__resonance (_state._state, value);
    }

    void _MoogLadder__resonance (_MoogLadder_State& _state, float v) noexcept
    {
        _state.res = v;
        _state.dirty = true;
    }

    void _sendEvent_drive (MoogLadder_State& _state, float value) noexcept
    {
        _MoogLadder__drive (_state._state, value);
    }

    void _MoogLadder__drive (_MoogLadder_State& _state, float v) noexcept
    {
        _state.drv = v;
    }

    void _sendEvent_slope (MoogLadder_State& _state, int32_t value) noexcept
    {
        _MoogLadder__slope (_state._state, value);
    }

    void _MoogLadder__slope (_MoogLadder_State& _state, int32_t v) noexcept
    {
        _state.slopeSel = v;
    }

    void _sendEvent_mode (MoogLadder_State& _state, int32_t value) noexcept
    {
        _MoogLadder__mode (_state._state, value);
    }

    void _MoogLadder__mode (_MoogLadder_State& _state, int32_t v) noexcept
    {
        _state.modeSel = v;
    }

    void _initialise (MoogLadder_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        _MoogLadder___initialise (_state._state, processorID, sessionID, frequency);
    }

    void _MoogLadder___initialise (_MoogLadder_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        _state.cutoff = 1000.0f;
        _state.dirty = true;
        _state.res = 0.0f;
        _state.drv = 0.0f;
        _state.slopeSel = int32_t {1};
        _state.modeSel = int32_t {0};
        _state.dcR = 0.998953f;
        _state.dcx1 = 0.0f;
        _state.dcy1 = 0.0f;
    }

    void _advance (MoogLadder_State& _state, MoogLadder_IO& _io, int32_t _frames) noexcept
    {
        _MoogLadder_IO  ioCopy;

        for (;;)
        {
            if (_state._currentFrame == _frames)
            {
                break;
            }
            ioCopy = _MoogLadder_IO {};
            ioCopy.in = _io.in[_state._currentFrame];
            _MoogLadder__main (_state._state, ioCopy);
            _io.out[_state._currentFrame] = ioCopy.out;
            ++_state._currentFrame;
        }
        _state._currentFrame = int32_t {0};
    }

    void _MoogLadder__main (_MoogLadder_State& _state, _MoogLadder_IO& _io) noexcept
    {
        bool  nlActive;
        float  o1;
        float  o2;
        float  o3;
        float  o4;
        float  gain;
        float  x_in;
        float  b1;
        float  b2;
        float  b3;
        float  b4;
        float  G2;
        float  G3;
        float  G4;
        float  B;
        float  O;
        float  y4cf;
        float  u1;
        float  y1;
        float  y2;
        float  y3;
        float  y4;
        float  tapOut;
        float  limOut;
        float  dcOut;
        float  b1_0;
        float  b2_0;
        float  b3_0;
        float  b4_0;
        float  G2_0;
        float  G3_0;
        float  G4_0;
        float  B_0;
        float  y4cf_0;
        float  u1_0;
        float  y1_0;
        float  y2_0;
        float  y3_0;
        float  y4_0;
        float  tapOut_0;
        float  limOut_0;
        float  dcOut_0;

        for (;;)
        {
            if (_state.dirty)
            {
                _MoogLadder__recompute (_state);
            }
            nlActive = (_state.drv > 0.0f) ? true : (_state.res > 0.0f);
            if (nlActive)
            {
                o1 = g_CALIB_NL * (_MoogLadder__padTanh (_state.yp1) - _state.yp1);
                o2 = g_CALIB_NL * (_MoogLadder__padTanh (_state.yp2) - _state.yp2);
                o3 = g_CALIB_NL * (_MoogLadder__padTanh (_state.yp3) - _state.yp3);
                o4 = g_CALIB_NL * (_MoogLadder__padTanh (_state.yp4) - _state.yp4);
                gain = 1.0f + (_state.drv * 3.0f);
                x_in = (_state.drv > 0.0f) ? _MoogLadder__padTanh (gain * _io.in) : _io.in;
                b1 = (1.0f - _state.G) * _state.s1;
                b2 = (1.0f - _state.G) * _state.s2;
                b3 = (1.0f - _state.G) * _state.s3;
                b4 = (1.0f - _state.G) * _state.s4;
                G2 = _state.G * _state.G;
                G3 = G2 * _state.G;
                G4 = G2 * G2;
                B = (((G3 * b1) + (G2 * b2)) + (_state.G * b3)) + b4;
                O = (((G4 * o1) + (G3 * o2)) + (G2 * o3)) + (_state.G * o4);
                y4cf = (((G4 * x_in) + O) + B) / (1.0f + (_state.r * G4));
                u1 = (x_in - (_state.r * y4cf)) + o1;
                y1 = (_state.G * (u1 - _state.s1)) + _state.s1;
                _state.s1 = ((2.0f * y1) - _state.s1);
                y2 = (_state.G * ((y1 + o2) - _state.s2)) + _state.s2;
                _state.s2 = ((2.0f * y2) - _state.s2);
                y3 = (_state.G * ((y2 + o3) - _state.s3)) + _state.s3;
                _state.s3 = ((2.0f * y3) - _state.s3);
                y4 = (_state.G * ((y3 + o4) - _state.s4)) + _state.s4;
                _state.s4 = ((2.0f * y4) - _state.s4);
                _state.yp1 = y1;
                _state.yp2 = y2;
                _state.yp3 = y3;
                _state.yp4 = y4;
                tapOut = (_state.modeSel == int32_t {1}) ? ((((g_CALIB_BP_W1 * y1) + (g_CALIB_BP_W2 * y2)) + (g_CALIB_BP_W3 * y3)) + (g_CALIB_BP_W4 * y4)) : ((_state.modeSel == int32_t {2}) ? (g_CALIB_HP_SCALE * ((((u1 - (4.0f * y1)) + (6.0f * y2)) - (4.0f * y3)) + y4)) : ((_state.slopeSel == int32_t {0}) ? y2 : y4));
                limOut = g_CALIB_LIM_CEIL * _MoogLadder__padTanh (tapOut / g_CALIB_LIM_CEIL);
                dcOut = (limOut - _state.dcx1) + (_state.dcR * _state.dcy1);
                _state.dcx1 = limOut;
                _state.dcy1 = dcOut;
                _io.out = (_io.out + dcOut);
            }
            else
            {
                b1_0 = (1.0f - _state.G) * _state.s1;
                b2_0 = (1.0f - _state.G) * _state.s2;
                b3_0 = (1.0f - _state.G) * _state.s3;
                b4_0 = (1.0f - _state.G) * _state.s4;
                G2_0 = _state.G * _state.G;
                G3_0 = G2_0 * _state.G;
                G4_0 = G2_0 * G2_0;
                B_0 = (((G3_0 * b1_0) + (G2_0 * b2_0)) + (_state.G * b3_0)) + b4_0;
                y4cf_0 = ((G4_0 * _io.in) + B_0) / (1.0f + (_state.r * G4_0));
                u1_0 = _io.in - (_state.r * y4cf_0);
                y1_0 = (_state.G * (u1_0 - _state.s1)) + _state.s1;
                _state.s1 = ((2.0f * y1_0) - _state.s1);
                y2_0 = (_state.G * (y1_0 - _state.s2)) + _state.s2;
                _state.s2 = ((2.0f * y2_0) - _state.s2);
                y3_0 = (_state.G * (y2_0 - _state.s3)) + _state.s3;
                _state.s3 = ((2.0f * y3_0) - _state.s3);
                y4_0 = (_state.G * (y3_0 - _state.s4)) + _state.s4;
                _state.s4 = ((2.0f * y4_0) - _state.s4);
                tapOut_0 = (_state.modeSel == int32_t {1}) ? ((((g_CALIB_BP_W1 * y1_0) + (g_CALIB_BP_W2 * y2_0)) + (g_CALIB_BP_W3 * y3_0)) + (g_CALIB_BP_W4 * y4_0)) : ((_state.modeSel == int32_t {2}) ? (g_CALIB_HP_SCALE * ((((u1_0 - (4.0f * y1_0)) + (6.0f * y2_0)) - (4.0f * y3_0)) + y4_0)) : ((_state.slopeSel == int32_t {0}) ? y2_0 : y4_0));
                limOut_0 = g_CALIB_LIM_CEIL * _MoogLadder__padTanh (tapOut_0 / g_CALIB_LIM_CEIL);
                dcOut_0 = (limOut_0 - _state.dcx1) + (_state.dcR * _state.dcy1);
                _state.dcx1 = limOut_0;
                _state.dcy1 = dcOut_0;
                _io.out = (_io.out + dcOut_0);
            }
            return;
        }
    }

    void _MoogLadder__recompute (_MoogLadder_State& _state) noexcept
    {
        float  sr;
        float  fc;
        float  R_MAX;
        float  resClamped;
        float  res2;
        float  rExcess;

        sr = static_cast<float> (1.0 * g__frequency);
        fc = intrinsics::clamp (_state.cutoff, 16.0f, sr * 0.45f);
        _state.g = intrinsics::tan ((3.1415927f * fc) / sr);
        R_MAX = 5.5f;
        resClamped = intrinsics::clamp (_state.res, 0.0f, 1.0f);
        res2 = resClamped * resClamped;
        _state.r = (((res2 * res2) * resClamped) * R_MAX);
        rExcess = intrinsics::max (0.0f, _state.r - 4.0f);
        _state.g = (_state.g * (1.0f - (0.06f * rExcess)));
        _state.G = (_state.g / (1.0f + _state.g));
        _state.dcR = (1.0f - (50.265484f / sr));
        _state.dirty = false;
    }

    float std__intrinsics__clamp (float value, float minimum, float maximum) noexcept
    {
        return (value > maximum) ? maximum : ((value < minimum) ? minimum : value);
    }

    float std__intrinsics__tan (float n) noexcept
    {
        {
            return intrinsics::sin (n) / intrinsics::cos (n);
        }
    }

    float std__intrinsics__sin (float n) noexcept
    {
        {
            return 0.0f;
        }
    }

    float std__intrinsics__cos (float n) noexcept
    {
        {
            return 0.0f;
        }
    }

    float std__intrinsics__max (float v1, float v2) noexcept
    {
        {
            return (v1 > v2) ? v1 : v2;
        }
    }

    float _MoogLadder__padTanh (float x) noexcept
    {
        float  x2;

        x2 = x * x;
        return intrinsics::clamp ((x * (27.0f + x2)) / (27.0f + (9.0f * x2)), -1.0f, 1.0f);
    }

    //==============================================================================
    const char* getStringForHandle (uint32_t handle, size_t& stringLength)
    {
        (void) handle; (void) stringLength;
        return "";
    }

    //==============================================================================
    int32_t g__sessionID {};
    double g__frequency {};
    static constexpr float g_CALIB_NL { 1.0f };
    static constexpr float g_CALIB_BP_W1 { 0.0f };
    static constexpr float g_CALIB_BP_W2 { 4.0f };
    static constexpr float g_CALIB_BP_W3 { -4.0f };
    static constexpr float g_CALIB_BP_W4 { 0.0f };
    static constexpr float g_CALIB_HP_SCALE { 1.0f };
    static constexpr float g_CALIB_LIM_CEIL { 1.5f };

    //==============================================================================
    struct intrinsics
    {
        template <typename T> static T modulo (T a, T b)
        {
            if constexpr (std::is_floating_point<T>::value)
                return std::fmod (a, b);
            else
                return a % b;
        }

        template <typename T> static T addModulo2Pi (T a, T b)
        {
            constexpr auto twoPi = static_cast<T> (3.141592653589793238 * 2);
            auto n = a + b;
            return n >= twoPi ? std::remainder (n, twoPi) : n;
        }

        template <typename T> static T abs           (T a)              { return std::abs (a); }
        template <typename T> static T min           (T a, T b)         { return std::min (a, b); }
        template <typename T> static T max           (T a, T b)         { return std::max (a, b); }
        template <typename T> static T clamp         (T a, T b, T c)    { return a < b ? b : (a > c ? c : a); }
        template <typename T> static T wrap          (T a, T b)         { if (b == 0) return 0; auto n = modulo (a, b); if (n < 0) n += b; return n; }
        template <typename T> static T fmod          (T a, T b)         { return b != 0 ? std::fmod (a, b) : 0; }
        template <typename T> static T remainder     (T a, T b)         { return b != 0 ? std::remainder (a, b) : 0; }
        template <typename T> static T floor         (T a)              { return std::floor (a); }
        template <typename T> static T ceil          (T a)              { return std::ceil (a); }
        template <typename T> static T rint          (T a)              { return std::rint (a); }
        template <typename T> static T sqrt          (T a)              { return std::sqrt (a); }
        template <typename T> static T pow           (T a, T b)         { return std::pow (a, b); }
        template <typename T> static T exp           (T a)              { return std::exp (a); }
        template <typename T> static T log           (T a)              { return std::log (a); }
        template <typename T> static T log10         (T a)              { return std::log10 (a); }
        template <typename T> static T sin           (T a)              { return std::sin (a); }
        template <typename T> static T cos           (T a)              { return std::cos (a); }
        template <typename T> static T tan           (T a)              { return std::tan (a); }
        template <typename T> static T sinh          (T a)              { return std::sinh (a); }
        template <typename T> static T cosh          (T a)              { return std::cosh (a); }
        template <typename T> static T tanh          (T a)              { return std::tanh (a); }
        template <typename T> static T asinh         (T a)              { return std::asinh (a); }
        template <typename T> static T acosh         (T a)              { return std::acosh (a); }
        template <typename T> static T atanh         (T a)              { return std::atanh (a); }
        template <typename T> static T asin          (T a)              { return std::asin (a); }
        template <typename T> static T acos          (T a)              { return std::acos (a); }
        template <typename T> static T atan          (T a)              { return std::atan (a); }
        template <typename T> static T atan2         (T a, T b)         { return std::atan2 (a, b); }
        template <typename T> static T isnan         (T a)              { return std::isnan (a) ? 1 : 0; }
        template <typename T> static T isinf         (T a)              { return std::isinf (a) ? 1 : 0; }
        template <typename T> static T select        (bool c, T a, T b) { return c ? a : b; }

        static int32_t reinterpretFloatToInt (float   a)                { int32_t i; memcpy (std::addressof(i), std::addressof(a), sizeof(i)); return i; }
        static int64_t reinterpretFloatToInt (double  a)                { int64_t i; memcpy (std::addressof(i), std::addressof(a), sizeof(i)); return i; }
        static float   reinterpretIntToFloat (int32_t a)                { float   f; memcpy (std::addressof(f), std::addressof(a), sizeof(f)); return f; }
        static double  reinterpretIntToFloat (int64_t a)                { double  f; memcpy (std::addressof(f), std::addressof(a), sizeof(f)); return f; }

        static int32_t rightShiftUnsigned (int32_t a, int32_t b)        { return static_cast<int32_t> (static_cast<uint32_t> (a) >> b); }
        static int64_t rightShiftUnsigned (int64_t a, int64_t b)        { return static_cast<int64_t> (static_cast<uint64_t> (a) >> b); }

        struct VectorOps
        {
            template <typename Vec> static Vec abs     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::abs (x); }); }
            template <typename Vec> static Vec min     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::min (x, y); }); }
            template <typename Vec> static Vec max     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::max (x, y); }); }
            template <typename Vec> static Vec sqrt    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sqrt (x); }); }
            template <typename Vec> static Vec log     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::log (x); }); }
            template <typename Vec> static Vec log10   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::log10 (x); }); }
            template <typename Vec> static Vec sin     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sin (x); }); }
            template <typename Vec> static Vec cos     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::cos (x); }); }
            template <typename Vec> static Vec tan     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::tan (x); }); }
            template <typename Vec> static Vec sinh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::sinh (x); }); }
            template <typename Vec> static Vec cosh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::cosh (x); }); }
            template <typename Vec> static Vec tanh    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::tanh (x); }); }
            template <typename Vec> static Vec asinh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::asinh (x); }); }
            template <typename Vec> static Vec acosh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::acosh (x); }); }
            template <typename Vec> static Vec atanh   (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::atanh (x); }); }
            template <typename Vec> static Vec asin    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::asin (x); }); }
            template <typename Vec> static Vec acos    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::acos (x); }); }
            template <typename Vec> static Vec atan    (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::atan (x); }); }
            template <typename Vec> static Vec atan2   (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::atan2 (x, y); }); }
            template <typename Vec> static Vec pow     (Vec a, Vec b)     { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::pow (x, y); }); }
            template <typename Vec> static Vec exp     (Vec a)            { return a.performUnaryOp ([] (auto x) { return intrinsics::exp (x); }); }

            template <typename Vec> static Vec rightShiftUnsigned (Vec a, Vec b) { return a.performBinaryOp (b, [] (auto x, auto y) { return intrinsics::rightShiftUnsigned (x, y); }); }
        };
    };

    static constexpr float  _inf32  =  std::numeric_limits<float>::infinity();
    static constexpr double _inf64  =  std::numeric_limits<double>::infinity();
    static constexpr float  _ninf32 = -std::numeric_limits<float>::infinity();
    static constexpr double _ninf64 = -std::numeric_limits<double>::infinity();
    static constexpr float  _nan32  =  std::numeric_limits<float>::quiet_NaN();
    static constexpr double _nan64  =  std::numeric_limits<double>::quiet_NaN();

    //==============================================================================
    #if __clang__
     #pragma clang diagnostic pop
    #elif __GNUC__
     #pragma GCC diagnostic pop
    #else
     #pragma warning (pop)
    #endif
};

