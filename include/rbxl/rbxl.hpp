#pragma once

// Umbrella header for library consumers: everything needed to load and save
// binary and XML Roblox files through the Result-based API in format.hpp.
// Exception-throwing sugar lives in a separate, opt-in header,
// <rbxl/throwing.hpp>, so that including this header never pulls in a
// throwing dependency.
#include <rbxl/result.hpp>
#include <rbxl/types.hpp>
#include <rbxl/variant.hpp>
#include <rbxl/dom.hpp>
#include <rbxl/compression.hpp>
#include <rbxl/version.hpp>
#include <rbxl/format.hpp>
