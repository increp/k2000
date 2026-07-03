
#include <cstdint>
#include <cmath>
#include <string>
#include <cstring>
#include <array>
#include <stdexcept>

//==============================================================================
/// Auto-generated C++ class for the 'NlSvfDrive' processor
///

#if ! (defined (__cplusplus) && (__cplusplus >= 201703L))
 #error "This code requires that your compiler is set to use C++17 or later!"
#endif

struct NlSvfDrive
{
    NlSvfDrive() = default;
    ~NlSvfDrive() = default;

    static constexpr std::string_view name = "NlSvfDrive";

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
    static constexpr uint32_t numInputEndpoints  = 8;
    static constexpr uint32_t numOutputEndpoints = 1;

    static constexpr uint32_t maxFramesPerBlock  = 512;
    static constexpr uint32_t eventBufferSize    = 32;
    static constexpr uint32_t maxOutputEventSize = 0;
    static constexpr double   latency            = 0.000000;

    enum class EndpointHandles
    {
        in         = 1,
        out        = 9,
        cutoffHz   = 2,
        resonance  = 3,
        resSat     = 4,
        tap        = 5,
        drive01    = 6,
        biasFixed  = 7,
        maxDriveDb = 8
    };

    static constexpr uint32_t getEndpointHandleForName (std::string_view endpointName)
    {
        if (endpointName == "in")          return static_cast<uint32_t> (EndpointHandles::in);
        if (endpointName == "out")         return static_cast<uint32_t> (EndpointHandles::out);
        if (endpointName == "cutoffHz")    return static_cast<uint32_t> (EndpointHandles::cutoffHz);
        if (endpointName == "resonance")   return static_cast<uint32_t> (EndpointHandles::resonance);
        if (endpointName == "resSat")      return static_cast<uint32_t> (EndpointHandles::resSat);
        if (endpointName == "tap")         return static_cast<uint32_t> (EndpointHandles::tap);
        if (endpointName == "drive01")     return static_cast<uint32_t> (EndpointHandles::drive01);
        if (endpointName == "biasFixed")   return static_cast<uint32_t> (EndpointHandles::biasFixed);
        if (endpointName == "maxDriveDb")  return static_cast<uint32_t> (EndpointHandles::maxDriveDb);
        return 0;
    }

    static constexpr EndpointInfo inputEndpoints[] =
    {
        { 1,  "in",          EndpointType::stream },
        { 2,  "cutoffHz",    EndpointType::event  },
        { 3,  "resonance",   EndpointType::event  },
        { 4,  "resSat",      EndpointType::event  },
        { 5,  "tap",         EndpointType::event  },
        { 6,  "drive01",     EndpointType::event  },
        { 7,  "biasFixed",   EndpointType::event  },
        { 8,  "maxDriveDb",  EndpointType::event  }
    };

    static constexpr EndpointInfo outputEndpoints[] =
    {
        { 9,  "out",  EndpointType::stream }
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
        inputEndpoints[5],
        inputEndpoints[6],
        inputEndpoints[7]
    };

    static constexpr std::array<EndpointInfo, 0> inputMIDIEvents {};

    static constexpr std::array inputParameters
    {
        inputEndpoints[1],
        inputEndpoints[2],
        inputEndpoints[3],
        inputEndpoints[4],
        inputEndpoints[5],
        inputEndpoints[6],
        inputEndpoints[7]
    };

    static constexpr const char* programDetailsJSON =
            "{\n"
            "  \"mainProcessor\": \"NlSvfDrive\",\n"
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
            "        \"max\": 0.999,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"resSat\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"ResSat\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 1,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"tap\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"int32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Tap\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 2,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"drive01\",\n"
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
            "      \"endpointID\": \"biasFixed\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"Bias\",\n"
            "        \"min\": -1,\n"
            "        \"max\": 1,\n"
            "        \"init\": 0\n"
            "      },\n"
            "      \"purpose\": \"parameter\"\n"
            "    },\n"
            "    {\n"
            "      \"endpointID\": \"maxDriveDb\",\n"
            "      \"endpointType\": \"event\",\n"
            "      \"dataType\": {\n"
            "        \"type\": \"float32\"\n"
            "      },\n"
            "      \"annotation\": {\n"
            "        \"name\": \"MaxDriveDb\",\n"
            "        \"min\": 0,\n"
            "        \"max\": 48,\n"
            "        \"init\": 30\n"
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
    struct _NlSvfDrive_State
    {
        float g = {};
        float k = {};
        float a1 = {};
        float a2 = {};
        float a3 = {};
        float ic1 = {};
        float ic2 = {};
        float bp = {};
        bool svfDirty = {};
        float cutoff = {};
        float res = {};
        float rsat = {};
        int32_t tapSel = {};
        float gain = {};
        float bias = {};
        float comp = {};
        bool drvDirty = {};
        float d01 = {};
        float biasF = {};
        float maxDb = {};
        int32_t _sessionID = {};
        double _frequency = {};
        int32_t _resumeIndex = {};
    };

    struct NlSvfDrive_State
    {
        int32_t _currentFrame = {};
        _NlSvfDrive_State _state;
    };

    struct NlSvfDrive_IO
    {
        Array<float, 512> in;
        Array<float, 512> out;
    };

    struct _NlSvfDrive_IO
    {
        float in = {};
        float out = {};
    };

    using std_intrinsics_T = float;
    using std_intrinsics_T_0 = float;
    using std_intrinsics_T_1 = float;
    using std_intrinsics_T_2 = float;
    using std_intrinsics_T_3 = float;
    using std_intrinsics_T_4 = float;
    using std_intrinsics_T_5 = float;
    using std_intrinsics_T_6 = float;
    using std_intrinsics_T_7 = float;
    using std_intrinsics_T_8 = float;

    //==============================================================================
    double getMaxFrequency() const
    {
        return 1536000.0;
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
        if (endpointHandle == 9) { std::memcpy (reinterpret_cast<char*> (dest), std::addressof (cmajIO.out), 4 * numFramesToCopy); std::memset (reinterpret_cast<char*> (std::addressof (cmajIO.out)), 0, 4 * numFramesToCopy); return; }
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

    void addEvent_resSat (float event)
    {
        _sendEvent_resSat (cmajState, event);
    }

    void addEvent_tap (int32_t event)
    {
        _sendEvent_tap (cmajState, event);
    }

    void addEvent_drive01 (float event)
    {
        _sendEvent_drive01 (cmajState, event);
    }

    void addEvent_biasFixed (float event)
    {
        _sendEvent_biasFixed (cmajState, event);
    }

    void addEvent_maxDriveDb (float event)
    {
        _sendEvent_maxDriveDb (cmajState, event);
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
            return addEvent_resSat (value);
        }

        if (endpointHandle == 5)
        {
            int32_t value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_tap (value);
        }

        if (endpointHandle == 6)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_drive01 (value);
        }

        if (endpointHandle == 7)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_biasFixed (value);
        }

        if (endpointHandle == 8)
        {
            float value;
            memcpy (&value, eventData, 4);
            eventData += 4;
            return addEvent_maxDriveDb (value);
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
    NlSvfDrive_State cmajState = {};
    NlSvfDrive_IO cmajIO = {};

    //==============================================================================
    void _sendEvent_cutoffHz (NlSvfDrive_State& _state, float value) noexcept
    {
        _NlSvfDrive__cutoffHz (_state._state, value);
    }

    void _NlSvfDrive__cutoffHz (_NlSvfDrive_State& _state, float v) noexcept
    {
        _state.cutoff = v;
        _state.svfDirty = true;
    }

    void _sendEvent_resonance (NlSvfDrive_State& _state, float value) noexcept
    {
        _NlSvfDrive__resonance (_state._state, value);
    }

    void _NlSvfDrive__resonance (_NlSvfDrive_State& _state, float v) noexcept
    {
        _state.res = v;
        _state.svfDirty = true;
    }

    void _sendEvent_resSat (NlSvfDrive_State& _state, float value) noexcept
    {
        _NlSvfDrive__resSat (_state._state, value);
    }

    void _NlSvfDrive__resSat (_NlSvfDrive_State& _state, float v) noexcept
    {
        _state.rsat = v;
    }

    void _sendEvent_tap (NlSvfDrive_State& _state, int32_t value) noexcept
    {
        _NlSvfDrive__tap (_state._state, value);
    }

    void _NlSvfDrive__tap (_NlSvfDrive_State& _state, int32_t v) noexcept
    {
        _state.tapSel = v;
    }

    void _sendEvent_drive01 (NlSvfDrive_State& _state, float value) noexcept
    {
        _NlSvfDrive__drive01 (_state._state, value);
    }

    void _NlSvfDrive__drive01 (_NlSvfDrive_State& _state, float v) noexcept
    {
        _state.d01 = v;
        _state.drvDirty = true;
    }

    void _sendEvent_biasFixed (NlSvfDrive_State& _state, float value) noexcept
    {
        _NlSvfDrive__biasFixed (_state._state, value);
    }

    void _NlSvfDrive__biasFixed (_NlSvfDrive_State& _state, float v) noexcept
    {
        _state.biasF = v;
        _state.drvDirty = true;
    }

    void _sendEvent_maxDriveDb (NlSvfDrive_State& _state, float value) noexcept
    {
        _NlSvfDrive__maxDriveDb (_state._state, value);
    }

    void _NlSvfDrive__maxDriveDb (_NlSvfDrive_State& _state, float v) noexcept
    {
        _state.maxDb = v;
        _state.drvDirty = true;
    }

    void _initialise (NlSvfDrive_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        _NlSvfDrive___initialise (_state._state, processorID, sessionID, frequency);
    }

    void _NlSvfDrive___initialise (_NlSvfDrive_State& _state, int32_t& processorID, int32_t sessionID, double frequency) noexcept
    {
        g__sessionID = sessionID;
        g__frequency = frequency;
        _state.cutoff = 1000.0f;
        _state.svfDirty = true;
        _state.res = 0.0f;
        _state.rsat = 0.0f;
        _state.tapSel = int32_t {0};
        _state.d01 = 0.0f;
        _state.drvDirty = true;
        _state.biasF = 0.0f;
        _state.maxDb = 30.0f;
        _state.gain = 1.0f;
        _state.bias = 0.0f;
        _state.comp = 1.0f;
    }

    void _advance (NlSvfDrive_State& _state, NlSvfDrive_IO& _io, int32_t _frames) noexcept
    {
        _NlSvfDrive_IO  ioCopy;

        for (;;)
        {
            if (_state._currentFrame == _frames)
            {
                break;
            }
            ioCopy = _NlSvfDrive_IO {};
            ioCopy.in = _io.in[_state._currentFrame];
            _NlSvfDrive__main (_state._state, ioCopy);
            _io.out[_state._currentFrame] = ioCopy.out;
            ++_state._currentFrame;
        }
        _state._currentFrame = int32_t {0};
    }

    void _NlSvfDrive__main (_NlSvfDrive_State& _state, _NlSvfDrive_IO& _io) noexcept
    {
        float  v0;
        float  bpPrev;
        float  v3;
        float  v1;
        float  v2;
        float  filt;

        for (;;)
        {
            if (_state.svfDirty)
            {
                _NlSvfDrive__recomputeSvf (_state);
            }
            if (_state.drvDirty)
            {
                _NlSvfDrive__recomputeDrive (_state);
            }
            v0 = _io.in;
            if (_state.rsat > 0.0f)
            {
                bpPrev = _state.bp;
                v0 = (v0 - ((intrinsics::abs (_state.k) * _state.rsat) * (bpPrev - _NlSvfDrive__satRes (bpPrev))));
            }
            v3 = v0 - _state.ic2;
            v1 = (_state.a1 * _state.ic1) + (_state.a2 * v3);
            v2 = (_state.ic2 + (_state.a2 * _state.ic1)) + (_state.a3 * v3);
            _state.ic1 = ((2.0f * v1) - _state.ic1);
            _state.ic2 = ((2.0f * v2) - _state.ic2);
            if (_state.rsat > 0.0f)
            {
                _state.ic1 = (_state.ic1 + (_state.rsat * (_NlSvfDrive__rail (_state.ic1) - _state.ic1)));
                _state.ic2 = (_state.ic2 + (_state.rsat * (_NlSvfDrive__rail (_state.ic2) - _state.ic2)));
            }
            _state.bp = v1;
            filt = {};
            if (_state.tapSel == int32_t {1})
            {
                filt = ((v0 - (_state.k * v1)) - v2);
            }
            else
            {
                if (_state.tapSel == int32_t {2})
                {
                    filt = v1;
                }
                else
                {
                    filt = v2;
                }
            }
            _io.out = (_io.out + (_state.comp * intrinsics::tanh ((_state.gain * filt) + _state.bias)));
            return;
        }
    }

    void _NlSvfDrive__recomputeSvf (_NlSvfDrive_State& _state) noexcept
    {
        float  sr;
        float  c;
        float  r;
        float  Q;
        float  oscStart;
        float  t;

        sr = static_cast<float> (1.0 * g__frequency);
        c = intrinsics::clamp (_state.cutoff, 16.0f, sr * 0.45f);
        r = intrinsics::clamp (_state.res, 0.0f, 0.999f);
        Q = 0.5f + ((r * r) * 49.5f);
        _state.g = intrinsics::tan ((3.1415927f * c) / sr);
        _state.k = (1.0f / Q);
        oscStart = 0.95f;
        if (r > oscStart)
        {
            t = (r - oscStart) / 0.050000012f;
            _state.k = ((_state.k * (1.0f - t)) - (0.012f * t));
        }
        _state.a1 = (1.0f / (1.0f + (_state.g * (_state.g + _state.k))));
        _state.a2 = (_state.g * _state.a1);
        _state.a3 = (_state.g * _state.a2);
        _state.svfDirty = false;
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

    void _NlSvfDrive__recomputeDrive (_NlSvfDrive_State& _state) noexcept
    {
        float  dB;
        float  full;

        dB = intrinsics::max (0.0f, _state.d01) * _state.maxDb;
        _state.gain = intrinsics::pow (10.0f, dB / 20.0f);
        _state.bias = _state.biasF;
        full = (_state.gain > 1.0f) ? (1.0f / intrinsics::tanh (_state.gain)) : 1.0f;
        _state.comp = (1.0f + (0.75f * (full - 1.0f)));
        _state.drvDirty = false;
    }

    float std__intrinsics__max (float v1, float v2) noexcept
    {
        {
            return (v1 > v2) ? v1 : v2;
        }
    }

    float std__intrinsics__pow (float a, float b) noexcept
    {
        {
            return 0.0f;
        }
    }

    float std__intrinsics__tanh (float n) noexcept
    {
        float  e;

        e = intrinsics::exp (intrinsics::min (n, 20.0f) * static_cast<float> (int32_t {2}));
        return (e - static_cast<float> (int32_t {1})) / (e + static_cast<float> (int32_t {1}));
    }

    float std__intrinsics__exp (float n) noexcept
    {
        {
            return 0.0f;
        }
    }

    float std__intrinsics__min (float v1, float v2) noexcept
    {
        {
            return (v1 < v2) ? v1 : v2;
        }
    }

    float std__intrinsics__abs (float n) noexcept
    {
        {
            return (n < static_cast<float> (int32_t {0})) ? (- n) : n;
        }
    }

    float _NlSvfDrive__satRes (float x) noexcept
    {
        float  b;
        float  s;

        b = 0.18f;
        s = 1.0f / _NlSvfDrive__padTanhDeriv (b);
        return (_NlSvfDrive__padTanh (x + b) - _NlSvfDrive__padTanh (b)) * s;
    }

    float _NlSvfDrive__padTanhDeriv (float x) noexcept
    {
        float  x2;
        float  den;
        float  num;

        x2 = x * x;
        den = 27.0f + (9.0f * x2);
        num = ((27.0f + (3.0f * x2)) * den) - (((27.0f * x) + (x * x2)) * (18.0f * x));
        return num / (den * den);
    }

    float _NlSvfDrive__padTanh (float x) noexcept
    {
        float  x2;

        x2 = x * x;
        return intrinsics::clamp ((x * (27.0f + x2)) / (27.0f + (9.0f * x2)), -1.0f, 1.0f);
    }

    float _NlSvfDrive__rail (float x) noexcept
    {
        return _NlSvfDrive__padTanh (x * 0.25f) * 4.0f;
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

