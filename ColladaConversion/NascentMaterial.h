// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "ConversionCore.h"
#include "../Core/Types.h"
#include <string>

namespace RenderCore { namespace ColladaConversion
{
    class ReferencedTexture
    {
    public:
        std::string     _resourceName;
        ReferencedTexture(const std::string& resourceName) : _resourceName(resourceName) {}
    };

    class ReferencedMaterial
    {
    public:
        typedef uint64 Guid;
        ObjectGuid      _effectId;
        Guid            _guid;
        std::string     _descriptiveName;

        ReferencedMaterial(
            const ObjectGuid& effectId,
            const Guid& guid,
            const std::string& descriptiveName)
        : _effectId(effectId), _guid(guid), _descriptiveName(descriptiveName) {}
    };

}}

