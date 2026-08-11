#pragma once

#include <limits>

#include "orderbook/Usings.h"

struct Constants {
    static constexpr Price InvalidPrice = std::numeric_limits<Price>::min();
};