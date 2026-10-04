#pragma once

#include <gtest/gtest.h>

#include "order_book.hpp"
#include "pooled_order_book.hpp"

// Every order-book implementation runs through the same typed test suites.
using BookTypes = ::testing::Types<limitbook::MapListOrderBook, limitbook::PooledOrderBook>;
