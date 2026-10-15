#pragma once
// Host build of shared Android-side code: the runner's system properties.

#define PROP_VALUE_MAX 92

int __system_property_get(const char* name, char* value);
