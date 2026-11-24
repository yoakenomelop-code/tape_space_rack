#pragma once
// Factory presets — the same Resources/presets.json the web version uses, compiled in as binary data.
// The JSON keys are the parameter keys, so values map 1:1.
#include <juce_core/juce_core.h>
#include "BinaryData.h"
#include "Params.h"

namespace ts
{
class PresetBank
{
public:
    PresetBank() { root = juce::JSON::parse (juce::String::fromUTF8 (TSData::presets_json, TSData::presets_jsonSize)); }

    int numModule (int type) const { auto* a = moduleArray (type); return a ? a->size() : 0; }
    juce::String moduleName (int type, int i) const { auto* a = moduleArray (type); return a && juce::isPositiveAndBelow (i, a->size()) ? (*a)[i]["name"].toString() : juce::String(); }
    juce::var moduleValues (int type, int i) const { auto* a = moduleArray (type); return a && juce::isPositiveAndBelow (i, a->size()) ? (*a)[i]["p"] : juce::var(); }
    juce::var moduleValuesByName (int type, const juce::String& name) const
    {
        if (auto* a = moduleArray (type)) for (auto& e : *a) if (e["name"].toString() == name) return e["p"];
        return {};
    }
    int numRack() const { auto* a = root["rack"].getArray(); return a ? a->size() : 0; }
    juce::String rackName (int i) const { auto* a = root["rack"].getArray(); return a && juce::isPositiveAndBelow (i, a->size()) ? (*a)[i]["name"].toString() : juce::String(); }
    juce::var rackSlots (int i) const { auto* a = root["rack"].getArray(); return a && juce::isPositiveAndBelow (i, a->size()) ? (*a)[i]["slots"] : juce::var(); }

    // apply JSON values on top of a Params struct
    static void apply (Params& p, const juce::var& v)
    {
        if (auto* o = v.getDynamicObject())
            for (auto& prop : o->getProperties())
                if (auto* sp = specOf (prop.name.toString())) p.*(sp->field) = (double) prop.value;
    }

private:
    const juce::Array<juce::var>* moduleArray (int type) const { return type >= 0 ? root["module"][typeKey (type)].getArray() : nullptr; }
    juce::var root;
};
} // namespace ts
