// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include <iomanip>
#include <ostream>

namespace Utility
{
    class ByteCount
    {
    public:
        explicit ByteCount(size_t size) : _size(size) {}
        size_t _size;

        friend inline std::ostream& operator<<(std::ostream& stream, const ByteCount& byteCount)
        {
            auto originalFlags = stream.flags();
            auto originalPrecision = stream.precision();
            auto s = byteCount._size;
            if (s > 512 * 1024 * 1024)  stream << std::setprecision(2) << std::fixed << s / float(1024 * 1024 * 1024) << " GiB";
            else if (s > 512 * 1024)    stream << std::setprecision(2) << std::fixed << s / float(1024 * 1024) << " MiB";
            else if (s > 512)           stream << std::setprecision(2) << std::fixed << s / float(1024) << " KiB";
            else                        stream << s << "B";
            stream.flags(originalFlags);
            stream.precision(originalPrecision);
            return stream;
        }
    };
}
using namespace Utility;
