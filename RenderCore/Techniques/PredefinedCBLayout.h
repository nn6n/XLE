// Copyright 2015 XLGAMES Inc.
//
// Distributed under the MIT License (See
// accompanying file "LICENSE" or the website
// http://www.opensource.org/licenses/mit-license.php)

#pragma once

#include "../../Assets/AssetUtils.h"
#include "../../Utility/ParameterBox.h"
#include "../../Utility/StringUtils.h"

namespace RenderCore { class SharedPkt; }
namespace RenderCore { namespace Techniques
{
    class PredefinedCBLayout
    {
    public:
        unsigned _cbSize;
        class Element
        {
        public:
            // std::basic_string<utf8> _name;
            ParameterBox::ParameterNameHash _hash;
            ImpliedTyping::TypeDesc _type;
            unsigned _offset;
        };
        std::vector<Element> _elements;
        ParameterBox _defaults;

        std::vector<uint8> BuildCBDataAsVector(const ParameterBox& parameters) const;
        SharedPkt BuildCBDataAsPkt(const ParameterBox& parameters) const;

        PredefinedCBLayout();
        PredefinedCBLayout(const ::Assets::ResChar initializer[]);
        PredefinedCBLayout(StringSection<char> source, bool);
        ~PredefinedCBLayout();

        PredefinedCBLayout(const PredefinedCBLayout&) = delete;
        PredefinedCBLayout& operator=(const PredefinedCBLayout&) = delete;
        PredefinedCBLayout(PredefinedCBLayout&&) never_throws;
        PredefinedCBLayout& operator=(PredefinedCBLayout&&) never_throws;

        const std::shared_ptr<::Assets::DependencyValidation>& GetDependencyValidation() const     
            { return _validationCallback; }

    private:
        std::shared_ptr<::Assets::DependencyValidation>   _validationCallback;

        void Parse(StringSection<char> source);
        void WriteBuffer(void* dst, const ParameterBox& parameters) const;
    };
}}
