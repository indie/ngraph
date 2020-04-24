//*****************************************************************************
// Copyright 2017-2020 Intel Corporation
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//*****************************************************************************

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <list>
#include <memory>

#include "gtest/gtest.h"
#include "ngraph/file_util.hpp"
#include "ngraph/graph_util.hpp"
#include "ngraph/log.hpp"
#include "ngraph/ngraph.hpp"
#include "ngraph/op/add.hpp"
#include "ngraph/op/batch_norm.hpp"
#include "ngraph/op/concat.hpp"
#include "ngraph/op/constant.hpp"
#include "ngraph/op/divide.hpp"
#include "ngraph/op/exp.hpp"
#include "ngraph/op/get_output_element.hpp"
#include "ngraph/op/log.hpp"
#include "ngraph/op/multiply.hpp"
#include "ngraph/op/negative.hpp"
#include "ngraph/op/product.hpp"
#include "ngraph/op/sqrt.hpp"
#include "ngraph/op/subtract.hpp"
#include "ngraph/op/sum.hpp"
#include "ngraph/pass/algebraic_simplification.hpp"
#include "ngraph/pass/graph_rewrite.hpp"
#include "ngraph/pass/manager.hpp"
#include "ngraph/pass/pass.hpp"
#include "ngraph/pass/visualize_tree.hpp"
#include "ngraph/pattern/matcher.hpp"
#include "ngraph/pattern/op/label.hpp"
#include "ngraph/pattern/op/skip.hpp"
#include "ngraph/serializer.hpp"
#include "util/all_close.hpp"
#include "util/matcher.hpp"
#include "util/test_tools.hpp"

using namespace ngraph;
using namespace std;

TEST(algebraic_simplification, add_types_shapes)
{
    Shape shapes[] = {Shape{}, Shape{2, 2}, Shape{3, 3, 3}};
    element::Type types[] = {element::i32, element::f32, element::f64};
    for (auto type : types)
    {
        for (auto shape : shapes)
        {
            pass::Manager pass_manager;
            pass_manager.register_pass<pass::AlgebraicSimplification>();

            auto a = make_shared<op::Parameter>(type, shape);
            auto b = make_shared<op::Parameter>(type, shape);
            auto c = make_shared<op::Parameter>(type, shape);
            auto iconst0 = ngraph::make_constant_from_string("0", type, shape);
            auto add_a_0 = make_shared<op::Abs>(a + iconst0);
            auto add_a_0_0 = add_a_0 + iconst0;
            auto add_b_0 = make_shared<op::Abs>(b + iconst0);
            auto add_b_0_0 = add_b_0 + iconst0;

            auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                                ParameterVector{a, b, c});
            pass_manager.run_passes(f);

            ASSERT_EQ(count_ops_of_type<op::Add>(f), 0);
            auto expected = ngraph::NodeVector{a, b, a, c, b};
            auto results = f->get_results();
            for (size_t i = 0; i < results.size(); i++)
            {
                ASSERT_EQ(expected.at(i),
                          (results.at(i)->get_argument(0)->input_values().size()
                               ? results.at(i)->get_argument(0)->get_argument(0)
                               : results.at(i)->get_argument(0)));
            }
        }
    }
}

TEST(algebraic_simplification, add_v1_types_shapes)
{
    Shape shapes[] = {Shape{}, Shape{2, 2}, Shape{3, 3, 3}};
    element::Type types[] = {element::i32, element::f32, element::f64};
    for (auto type : types)
    {
        for (auto shape : shapes)
        {
            pass::Manager pass_manager;
            pass_manager.register_pass<pass::Validate>();
            pass_manager.register_pass<pass::AlgebraicSimplification>();

            auto a = make_shared<op::Parameter>(type, shape);
            auto b = make_shared<op::Parameter>(type, shape);
            auto c = make_shared<op::Parameter>(type, shape);
            auto iconst0 = ngraph::make_constant_from_string("0", type, shape);
            auto add_a_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(a, iconst0));
            auto add_a_0_0 = make_shared<op::v1::Add>(add_a_0, iconst0);
            auto add_b_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(b, iconst0));
            auto add_b_0_0 = make_shared<op::v1::Add>(add_b_0, iconst0);

            auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                                ParameterVector{a, b, c});
            pass_manager.run_passes(f);

            ASSERT_EQ(count_ops_of_type<op::v1::Add>(f), 0);
            auto expected = ngraph::NodeVector{a, b, a, c, b};
            auto results = f->get_results();
            for (size_t i = 0; i < results.size(); i++)
            {
                ASSERT_EQ(expected.at(i),
                          (results.at(i)->get_argument(0)->input_values().size()
                               ? results.at(i)->get_argument(0)->get_argument(0)
                               : results.at(i)->get_argument(0)));
            }
        }
    }
}

TEST(algebraic_simplification, add_broadcast)
{
    Shape shape{2, 2};
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::Validate>();
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(element::i32, shape);
    auto b = make_shared<op::Parameter>(element::i32, shape);
    auto c = make_shared<op::Parameter>(element::i32, shape);
    auto iconst0 = ngraph::make_zero(element::i32, Shape{});
    auto const_broadcast = make_shared<op::Broadcast>(iconst0, shape, AxisSet{0, 1});
    auto add_a_0 = make_shared<op::Abs>(a + const_broadcast);
    auto add_a_0_0 = add_a_0 + const_broadcast;
    auto add_b_0 = make_shared<op::Abs>(b + const_broadcast);
    auto add_b_0_0 = add_b_0 + const_broadcast;

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_EQ(count_ops_of_type<op::Add>(f), 0);
    auto expected = ngraph::NodeVector{a, b, a, c, b};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i),
                  (results.at(i)->get_argument(0)->input_values().size()
                       ? results.at(i)->get_argument(0)->get_argument(0)
                       : results.at(i)->get_argument(0)));
    }
}

TEST(algebraic_simplification, add_v1_broadcast_v1)
{
    Shape shape{2, 2};
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::Validate>();
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(element::i32, shape);
    auto b = make_shared<op::Parameter>(element::i32, shape);
    auto c = make_shared<op::Parameter>(element::i32, shape);
    auto iconst0 = ngraph::make_zero(element::i32, Shape{});
    auto target_shape = op::Constant::create<int64_t>(element::i64, Shape{2}, {2, 2});
    auto const_broadcast = make_shared<op::v1::Broadcast>(iconst0, target_shape);
    auto add_a_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(a, const_broadcast));
    auto add_a_0_0 = make_shared<op::v1::Add>(add_a_0, const_broadcast);
    auto add_b_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(b, const_broadcast));
    auto add_b_0_0 = make_shared<op::v1::Add>(add_b_0, const_broadcast);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_EQ(count_ops_of_type<op::v1::Add>(f), 0);
    auto expected = ngraph::NodeVector{a, b, a, c, b};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i),
                  (results.at(i)->get_argument(0)->input_values().size()
                       ? results.at(i)->get_argument(0)->get_argument(0)
                       : results.at(i)->get_argument(0)));
    }
}

TEST(algebraic_simplification, multiply_broadcast_0)
{
    Shape shape{2, 2};
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::Validate>();
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(element::i32, shape);
    auto b = make_shared<op::Parameter>(element::i32, shape);
    auto c = make_shared<op::Parameter>(element::i32, shape);
    auto iconst0 = ngraph::make_zero(element::i32, Shape{});
    auto const_broadcast = make_shared<op::Broadcast>(iconst0, shape, AxisSet{0, 1});
    auto mul_a_0 = make_shared<op::Abs>(a * const_broadcast);
    auto mul_a_0_0 = make_shared<op::Abs>(mul_a_0 * const_broadcast);
    auto mul_b_0 = make_shared<op::Abs>(b * const_broadcast);
    auto mul_b_0_0 = make_shared<op::Abs>(mul_b_0 * const_broadcast);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, mul_a_0_0, c, mul_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_EQ(count_ops_of_type<op::Multiply>(f), 0);
    auto expected = ngraph::NodeVector{a, b, const_broadcast, c, const_broadcast};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i),
                  (results.at(i)->get_argument(0)->input_values().size()
                       ? results.at(i)->get_argument(0)->get_argument(0)
                       : results.at(i)->get_argument(0)));
    }
}

TEST(algebraic_simplification, multiply_v1_broadcast_v1_0)
{
    Shape shape{2, 2};
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(element::i32, shape);
    auto b = make_shared<op::Parameter>(element::i32, shape);
    auto c = make_shared<op::Parameter>(element::i32, shape);
    auto iconst0 = ngraph::make_zero(element::i32, Shape{});
    auto target_shape = op::Constant::create<int64_t>(element::i64, Shape{2}, {2, 2});
    auto const_broadcast = make_shared<op::v1::Broadcast>(iconst0, target_shape);
    auto mul_a_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(a, const_broadcast));
    auto mul_a_0_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(mul_a_0, const_broadcast));
    auto mul_b_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(b, const_broadcast));
    auto mul_b_0_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(mul_b_0, const_broadcast));

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, mul_a_0_0, c, mul_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_EQ(count_ops_of_type<op::v1::Multiply>(f), 0);
    auto expected = ngraph::NodeVector{a, b, const_broadcast, c, const_broadcast};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i),
                  (results.at(i)->get_argument(0)->input_values().size()
                       ? results.at(i)->get_argument(0)->get_argument(0)
                       : results.at(i)->get_argument(0)));
    }
}

TEST(algebraic_simplification, multiply_broadcast_1)
{
    Shape shape{2, 2};
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(element::i32, shape);
    auto b = make_shared<op::Parameter>(element::i32, shape);
    auto c = make_shared<op::Parameter>(element::i32, shape);
    auto const_broadcast = ngraph::builder::make_constant<int32_t>(element::i32, shape, 1);
    auto mul_a_0 = make_shared<op::Abs>(a * const_broadcast);
    auto mul_a_0_0 = mul_a_0 * const_broadcast;
    auto mul_b_0 = make_shared<op::Abs>(b * const_broadcast);
    auto mul_b_0_0 = mul_b_0 * const_broadcast;

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, mul_a_0_0, c, mul_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_EQ(count_ops_of_type<op::Multiply>(f), 0);
    auto expected = ngraph::NodeVector{a, b, a, c, b};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i),
                  (results.at(i)->get_argument(0)->input_values().size()
                       ? results.at(i)->get_argument(0)->get_argument(0)
                       : results.at(i)->get_argument(0)));
    }
}

TEST(algebraic_simplification, multiply_v1_broadcast_v1_1)
{
    Shape shape{2, 2};
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(element::i32, shape);
    auto b = make_shared<op::Parameter>(element::i32, shape);
    auto c = make_shared<op::Parameter>(element::i32, shape);
    auto const_broadcast = ngraph::builder::make_constant<int32_t>(element::i32, shape, 1);
    auto mul_a_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(a, const_broadcast));
    auto mul_a_0_0 = make_shared<op::v1::Multiply>(mul_a_0, const_broadcast);
    auto mul_b_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(b, const_broadcast));
    auto mul_b_0_0 = make_shared<op::v1::Multiply>(mul_b_0, const_broadcast);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, mul_a_0_0, c, mul_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_EQ(count_ops_of_type<op::v1::Multiply>(f), 0);
    auto expected = ngraph::NodeVector{a, b, a, c, b};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i),
                  (results.at(i)->get_argument(0)->input_values().size()
                       ? results.at(i)->get_argument(0)->get_argument(0)
                       : results.at(i)->get_argument(0)));
    }
}

TEST(algebraic_simplification, zero_plus_zero_commutativity)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto iconst0 = ngraph::make_constant_from_string("0", type, shape);
    auto add_a_0 = make_shared<op::Abs>(iconst0 + iconst0);
    auto add_a_0_0 = make_shared<op::Abs>(iconst0 + iconst0);
    auto add_b_0 = make_shared<op::Abs>(iconst0 + b);
    auto add_b_0_0 = make_shared<op::Abs>(iconst0 + b);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_TRUE(ngraph::is_zero(f->get_results().at(2)->get_argument(0)->get_argument(0)));
    ASSERT_EQ(f->get_results().at(4)->get_argument(0)->get_argument(0), b);
}

TEST(algebraic_simplification, zero_plus_zero_commutativity_v1)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto iconst0 = ngraph::make_constant_from_string("0", type, shape);
    auto add_a_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(iconst0, iconst0));
    auto add_a_0_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(iconst0, iconst0));
    auto add_b_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(iconst0, b));
    auto add_b_0_0 = make_shared<op::Abs>(make_shared<op::v1::Add>(iconst0, b));

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_TRUE(ngraph::is_zero(f->get_results().at(2)->get_argument(0)->get_argument(0)));
    ASSERT_EQ(f->get_results().at(4)->get_argument(0)->get_argument(0), b);
}

TEST(algebraic_simplification, zero_multiply_zero_one)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto iconst0 = ngraph::make_constant_from_string("0", type, shape);
    auto iconst1 = ngraph::make_constant_from_string("1", type, shape);
    auto add_a_0 = make_shared<op::Abs>(iconst0 * iconst0);
    auto add_b_0 = make_shared<op::Abs>(iconst1 * iconst0);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0, c, add_b_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_TRUE(ngraph::is_zero(f->get_results().at(2)->get_argument(0)->get_argument(0)));
    ASSERT_TRUE(ngraph::is_zero(f->get_results().at(4)->get_argument(0)->get_argument(0)));
}

TEST(algebraic_simplification, zero_multiply_zero_one_v1)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto iconst0 = ngraph::make_constant_from_string("0", type, shape);
    auto iconst1 = ngraph::make_constant_from_string("1", type, shape);
    auto add_a_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(iconst0, iconst0));
    auto add_b_0 = make_shared<op::Abs>(make_shared<op::v1::Multiply>(iconst1, iconst0));

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0, c, add_b_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    ASSERT_TRUE(ngraph::is_zero(f->get_results().at(2)->get_argument(0)->get_argument(0)));
    ASSERT_TRUE(ngraph::is_zero(f->get_results().at(4)->get_argument(0)->get_argument(0)));
}

TEST(algebraic_simplification, add_negative_tests)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto abs_a = make_shared<op::Abs>(a);
    auto iconst2 = ngraph::make_constant_from_string("2", type, shape);
    auto add_a_0 = a + iconst2;
    auto add_a_0_0 = add_a_0 + iconst2;
    auto add_b_0 = b + abs_a;
    auto add_b_0_0 = add_b_0 + abs_a;

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    auto expected = ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i), results.at(i)->get_argument(0));
    }
}

TEST(algebraic_simplification, add_negative_tests_v1)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto abs_a = make_shared<op::Abs>(a);
    auto iconst2 = ngraph::make_constant_from_string("2", type, shape);
    auto add_a_0 = make_shared<op::v1::Add>(a, iconst2);
    auto add_a_0_0 = make_shared<op::v1::Add>(add_a_0, iconst2);
    auto add_b_0 = make_shared<op::v1::Add>(b, abs_a);
    auto add_b_0_0 = make_shared<op::v1::Add>(add_b_0, abs_a);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    auto expected = ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i), results.at(i)->get_argument(0));
    }
}

TEST(algebraic_simplification, multiply_negative_tests_v1)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto abs_a = make_shared<op::Abs>(a);
    auto iconst2 = ngraph::make_constant_from_string("2", type, shape);
    auto add_a_0 = make_shared<op::v1::Multiply>(a, iconst2);
    auto add_a_0_0 = make_shared<op::v1::Multiply>(add_a_0, iconst2);
    auto add_b_0 = make_shared<op::v1::Multiply>(b, abs_a);
    auto add_b_0_0 = make_shared<op::v1::Multiply>(add_b_0, abs_a);

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    auto expected = ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i), results.at(i)->get_argument(0));
    }
}

TEST(algebraic_simplification, multiply_negative_tests)
{
    Shape shape{};
    auto type = element::f32;
    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto a = make_shared<op::Parameter>(type, shape);
    auto b = make_shared<op::Parameter>(type, shape);
    auto c = make_shared<op::Parameter>(type, shape);
    auto abs_a = make_shared<op::Abs>(a);
    auto iconst2 = ngraph::make_constant_from_string("2", type, shape);
    auto add_a_0 = a * iconst2;
    auto add_a_0_0 = add_a_0 * iconst2;
    auto add_b_0 = b * abs_a;
    auto add_b_0_0 = add_b_0 * abs_a;

    auto f = std::make_shared<Function>(ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0},
                                        ParameterVector{a, b, c});
    pass_manager.run_passes(f);

    auto expected = ngraph::NodeVector{a, b, add_a_0_0, c, add_b_0_0};
    auto results = f->get_results();
    for (size_t i = 0; i < results.size(); i++)
    {
        ASSERT_EQ(expected.at(i), results.at(i)->get_argument(0));
    }
}

TEST(algebraic_simplification, multiply_prod_vector_one)
{
    auto fconst1 = ngraph::op::Constant::create(element::f64, Shape{}, {2.0});
    auto broadcast = std::make_shared<op::Broadcast>(fconst1, Shape{3, 5}, AxisSet{0, 1});
    auto prod_fconst1 = std::make_shared<op::Product>(broadcast, AxisSet{1});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{prod_fconst1}, ParameterVector{});
    pass_manager.run_passes(f);
    auto new_broadcast = as_type_ptr<op::Broadcast>(f->get_results().at(0)->get_argument(0));
    ASSERT_TRUE(new_broadcast);
    auto new_const = as_type_ptr<op::Constant>(new_broadcast->get_argument(0));
    auto values = new_const->get_vector<double>();
    ASSERT_EQ(values.size(), 1);
    ASSERT_EQ(values.at(0), 32);
}

TEST(algebraic_simplification, multiply_prod_scalar_one)
{
    auto fconst1 = ngraph::op::Constant::create(element::f64, Shape{}, {2.0});
    auto broadcast = std::make_shared<op::Broadcast>(fconst1, Shape{3, 5}, AxisSet{0, 1});
    auto prod_fconst1 = std::make_shared<op::Product>(broadcast, AxisSet{0, 1});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{prod_fconst1}, ParameterVector{});
    pass_manager.run_passes(f);
    auto new_const = as_type_ptr<op::Constant>(f->get_results().at(0)->get_argument(0));
    ASSERT_TRUE(new_const);
    auto values = new_const->get_vector<double>();
    ASSERT_EQ(values.size(), 1);
    ASSERT_EQ(values.at(0), 32768);
}

TEST(algebraic_simplification, multiply_prod_negative)
{
    auto fconst1 = ngraph::op::Constant::create(element::f64, Shape{2}, {1.0, 1.0});
    auto broadcast = std::make_shared<op::Broadcast>(fconst1, Shape{2, 5}, AxisSet{1});
    auto prod_fconst1 = std::make_shared<op::Product>(broadcast, AxisSet{0, 1});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{prod_fconst1}, ParameterVector{});
    pass_manager.run_passes(f);
    auto f_prod = f->get_results().at(0)->get_argument(0);
    ASSERT_EQ(f_prod, prod_fconst1);
}

TEST(algebraic_simplification, multiply_sum_scalar_one)
{
    auto fconst1 = ngraph::op::Constant::create(element::f64, Shape{}, {1.0});
    auto broadcast = std::make_shared<op::Broadcast>(fconst1, Shape{3, 5}, AxisSet{0, 1});
    auto sum_fconst1 = std::make_shared<op::Sum>(broadcast, AxisSet{0, 1});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{sum_fconst1}, ParameterVector{});
    pass_manager.run_passes(f);
    auto new_const = as_type_ptr<op::Constant>(f->get_results().at(0)->get_argument(0));
    ASSERT_TRUE(new_const);
    auto values = new_const->get_vector<double>();
    ASSERT_EQ(values.size(), 1);
    ASSERT_EQ(values.at(0), 15);
}

TEST(algebraic_simplification, multiply_sum_vector_one)
{
    auto fconst1 = ngraph::op::Constant::create(element::f64, Shape{}, {1.0});
    auto broadcast = std::make_shared<op::Broadcast>(fconst1, Shape{3, 5}, AxisSet{0, 1});
    auto sum_fconst1 = std::make_shared<op::Sum>(broadcast, AxisSet{1});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{sum_fconst1}, ParameterVector{});
    pass_manager.run_passes(f);
    auto new_broadcast = as_type_ptr<op::Broadcast>(f->get_results().at(0)->get_argument(0));
    ASSERT_TRUE(new_broadcast);
    auto new_const = as_type_ptr<op::Constant>(new_broadcast->get_argument(0));
    auto values = new_const->get_vector<double>();
    ASSERT_EQ(values.size(), 1);
    ASSERT_EQ(values.at(0), 5);
}

TEST(algebraic_simplification, multiply_sum_negative)
{
    auto fconst1 = ngraph::op::Constant::create(element::f64, Shape{2}, {1.0, 1.0});
    auto broadcast = std::make_shared<op::Broadcast>(fconst1, Shape{2, 5}, AxisSet{1});
    auto sum_fconst1 = std::make_shared<op::Sum>(broadcast, AxisSet{0, 1});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{sum_fconst1}, ParameterVector{});
    pass_manager.run_passes(f);
    auto f_sum = f->get_results().at(0)->get_argument(0);
    ASSERT_EQ(f_sum, sum_fconst1);
}

TEST(algebraic_simplification, concat_reshape_slice)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto slice1 = make_shared<op::Slice>(a, Coordinate{0, 0}, Coordinate{32, 100}, Strides{1, 1});
    auto slice2 = make_shared<op::Slice>(a, Coordinate{32, 0}, Coordinate{64, 100}, Strides{1, 1});
    auto slice3 = make_shared<op::Slice>(a, Coordinate{64, 0}, Coordinate{96, 100}, Strides{1, 1});

    auto reshape1 = make_shared<op::Reshape>(slice1, AxisVector{0, 1}, Shape{32, 1, 100});
    auto reshape2 = make_shared<op::Reshape>(slice2, AxisVector{0, 1}, Shape{32, 1, 100});
    auto reshape3 = make_shared<op::Reshape>(slice3, AxisVector{0, 1}, Shape{32, 1, 100});

    size_t concat_axis = 1;
    auto concat = make_shared<op::Concat>(NodeVector{reshape1, reshape2, reshape3}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_TRUE(is_type<op::Reshape>(f->get_results().at(0)->get_argument(0)));
}

TEST(algebraic_simplification, concat_slice)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto slice1 = make_shared<op::Slice>(a, Coordinate{0, 0}, Coordinate{32, 100}, Strides{1, 1});
    auto slice2 = make_shared<op::Slice>(a, Coordinate{32, 0}, Coordinate{64, 100}, Strides{1, 1});
    auto slice3 = make_shared<op::Slice>(a, Coordinate{64, 0}, Coordinate{96, 100}, Strides{1, 1});

    size_t concat_axis = 0;
    auto concat = make_shared<op::Concat>(NodeVector{slice1, slice2, slice3}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0), a);
}

TEST(algebraic_simplification, concat_parameter_slice)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto slice1 = make_shared<op::Slice>(a, Coordinate{0, 0}, Coordinate{32, 100}, Strides{1, 1});
    auto slice2 = make_shared<op::Slice>(a, Coordinate{32, 0}, Coordinate{64, 100}, Strides{1, 1});
    auto slice3 = make_shared<op::Slice>(a, Coordinate{64, 0}, Coordinate{96, 100}, Strides{1, 1});

    size_t concat_axis = 0;
    auto concat = make_shared<op::Concat>(NodeVector{slice1, slice2, slice3}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0), a);
}

TEST(algebraic_simplification, concat_parameter_slices_reversed)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto slice1 = make_shared<op::Slice>(a, Coordinate{0, 0}, Coordinate{32, 100}, Strides{1, 1});
    auto slice2 = make_shared<op::Slice>(a, Coordinate{32, 0}, Coordinate{64, 100}, Strides{1, 1});
    auto slice3 = make_shared<op::Slice>(a, Coordinate{64, 0}, Coordinate{96, 100}, Strides{1, 1});

    size_t concat_axis = 0;
    auto concat = make_shared<op::Concat>(NodeVector{slice3, slice2, slice1}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0), concat);
}

TEST(algebraic_simplification, concat_parameter_slices_element_count)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    // slicing 30 elements out of 96; should trigger a check that some elements are missing
    auto slice1 = make_shared<op::Slice>(a, Coordinate{0, 0}, Coordinate{10, 100}, Strides{1, 1});
    auto slice2 = make_shared<op::Slice>(a, Coordinate{10, 0}, Coordinate{20, 100}, Strides{1, 1});
    auto slice3 = make_shared<op::Slice>(a, Coordinate{20, 0}, Coordinate{30, 100}, Strides{1, 1});

    size_t concat_axis = 0;
    auto concat = make_shared<op::Concat>(NodeVector{slice1, slice2, slice3}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0), concat);
}

TEST(algebraic_simplification, concat_parameter_non_uniform_slices)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto slice1 = make_shared<op::Slice>(a, Coordinate{0, 0}, Coordinate{38, 100}, Strides{1, 1});
    auto slice2 = make_shared<op::Slice>(a, Coordinate{38, 0}, Coordinate{64, 100}, Strides{1, 1});
    auto slice3 = make_shared<op::Slice>(a, Coordinate{64, 0}, Coordinate{96, 100}, Strides{1, 1});

    size_t concat_axis = 0;
    auto concat = make_shared<op::Concat>(NodeVector{slice1, slice2, slice3}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0), concat);
}

TEST(algebraic_simplification, concat_different_inputs)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto goe1 = -a;
    auto goe2 = -a;
    auto slice1 =
        make_shared<op::Slice>(goe1, Coordinate{0, 0}, Coordinate{32, 100}, Strides{1, 1});
    auto slice2 =
        make_shared<op::Slice>(goe2, Coordinate{32, 0}, Coordinate{64, 100}, Strides{1, 1});
    auto slice3 =
        make_shared<op::Slice>(goe1, Coordinate{64, 0}, Coordinate{96, 100}, Strides{1, 1});

    size_t concat_axis = 0;
    auto concat = make_shared<op::Concat>(NodeVector{slice1, slice2, slice3}, concat_axis);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{concat}, ParameterVector{a});
    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0), concat);
}

TEST(algebraic_simplification, log_neg_neg)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto b = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto exp_a = make_shared<op::Exp>(a);
    auto div = exp_a / b;
    auto log_div = make_shared<op::Log>(div);

    auto neg_inner = make_shared<op::Negative>(log_div);
    auto neg2 = make_shared<op::Negative>(neg_inner);
    auto neg3 = make_shared<op::Negative>(neg2);
    auto neg4 = make_shared<op::Negative>(neg3);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{neg4}, ParameterVector{a, b});
    pass_manager.run_passes(f);
    auto sub = as_type_ptr<op::Subtract>(neg_inner->get_argument(0));
    ASSERT_TRUE(sub != nullptr);
    ASSERT_EQ(sub->get_argument(0), a);
    auto new_log = as_type_ptr<op::Log>(sub->get_argument(1));
    ASSERT_TRUE(new_log != nullptr);
    ASSERT_EQ(new_log->get_argument(0), b);
}

TEST(algebraic_simplification, log_no_exp)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto b = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto abs_a = make_shared<op::Abs>(a);
    auto div = abs_a / b;
    auto log_div = make_shared<op::Log>(div);

    auto neg_inner = make_shared<op::Negative>(log_div);
    auto neg2 = make_shared<op::Negative>(neg_inner);
    auto neg3 = make_shared<op::Negative>(neg2);
    auto neg4 = make_shared<op::Negative>(neg3);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{neg4}, ParameterVector{a, b});
    pass_manager.run_passes(f);
    ASSERT_EQ(neg_inner->get_argument(0), log_div);
}

TEST(algebraic_simplification, log_no_divide)
{
    auto a = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto b = make_shared<op::Parameter>(element::f32, Shape{96, 100});
    auto exp_a = make_shared<op::Exp>(a);
    auto mul = exp_a * b;
    auto log_mul = make_shared<op::Log>(mul);

    auto neg_inner = make_shared<op::Negative>(log_mul);
    auto neg2 = make_shared<op::Negative>(neg_inner);
    auto neg3 = make_shared<op::Negative>(neg2);
    auto neg4 = make_shared<op::Negative>(neg3);

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    auto f = std::make_shared<Function>(ngraph::NodeVector{neg4}, ParameterVector{a, b});
    pass_manager.run_passes(f);
    ASSERT_EQ(neg_inner->get_argument(0), log_mul);
}

TEST(algebraic_simplification, pass_property)
{
    auto pass = std::make_shared<ngraph::pass::AlgebraicSimplification>();

    ASSERT_FALSE(pass->get_property(pass::PassProperty::CHANGE_DYNAMIC_STATE));
}

// the following gather test will be used to test when
// gather is Nop and will be removed during `simplify_gather`
// algebraic_simplification pass

TEST(algebraic_simplification, gather_3d_axis_default)
{
    Shape params_shape{1, 3, 2};
    Shape indices_shape{1};
    Shape out_shape{1, 3, 2};
    auto P = make_shared<op::Parameter>(element::f32, params_shape);
    auto I = make_shared<op::Parameter>(element::i32, indices_shape);
    auto axes = op::Constant::create(element::i64, Shape{}, {0});
    auto G = make_shared<op::v1::Gather>(P, I, axes);
    auto f = make_shared<Function>(make_shared<op::v0::Abs>(G), ParameterVector{P, I});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0)->get_argument(0), P);

    // the pass should short cut the Gather i/p with the gather users
    // since we are fetching the whole tensor using gather op
    auto gather_ops = get_ops_of_type<op::v1::Gather>(f);
    EXPECT_EQ(gather_ops.size(), 0);
}

TEST(algebraic_simplification, gather_3d_axis_1_nop)
{
    Shape params_shape{3, 1, 2};
    Shape indices_shape{1};
    Shape out_shape{3, 1, 2};
    auto P = make_shared<op::Parameter>(element::f32, params_shape);
    auto I = make_shared<op::Parameter>(element::i32, indices_shape);
    auto axes = op::Constant::create(element::i64, Shape{}, {1});
    auto G = make_shared<op::v1::Gather>(P, I, axes);
    auto f = make_shared<Function>(make_shared<op::v0::Abs>(G), ParameterVector{P, I});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0)->get_argument(0), P);

    // the pass should short cut the Gather i/p with the gather users
    // since we are fetching the whole tensor using gather op
    auto gather_ops = get_ops_of_type<op::v1::Gather>(f);
    EXPECT_EQ(gather_ops.size(), 0);
}

TEST(algebraic_simplification, gather_3d_axis_2_nop)
{
    Shape params_shape{3, 2, 1};
    Shape indices_shape{1};
    Shape out_shape{3, 2, 1};
    auto P = make_shared<op::Parameter>(element::f32, params_shape);
    auto I = make_shared<op::Parameter>(element::i32, indices_shape);
    auto axes = op::Constant::create(element::i64, Shape{}, {2});
    auto G = make_shared<op::v1::Gather>(P, I, axes);
    auto f = make_shared<Function>(make_shared<op::v0::Abs>(G), ParameterVector{P, I});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0)->get_argument(0), P);

    // the pass should short cut the Gather i/p with the gather users
    // since we are fetching the whole tensor using gather op
    auto gather_ops = get_ops_of_type<op::v1::Gather>(f);
    EXPECT_EQ(gather_ops.size(), 0);
}

TEST(algebraic_simplification, gather_3d_indices_constant_axis_1)
{
    Shape params_shape{3, 2, 1};
    Shape indices_shape{2};
    Shape out_shape{3, 2, 1};
    auto P = make_shared<op::Parameter>(element::f32, params_shape);
    auto I = op::Constant::create<int64_t>(element::i64, Shape{2}, {0, 1});
    auto axes = op::Constant::create(element::i64, Shape{}, {1});
    auto G = make_shared<op::v1::Gather>(P, I, axes);
    auto f = make_shared<Function>(make_shared<op::v0::Abs>(G), ParameterVector{P});

    pass::Manager pass_manager;
    pass_manager.register_pass<pass::AlgebraicSimplification>();

    pass_manager.run_passes(f);
    ASSERT_EQ(f->get_results().at(0)->get_argument(0)->get_argument(0), P);

    // the pass should short cut the Gather i/p with the gather users
    // since we are fetching the whole tensor using gather op
    auto gather_ops = get_ops_of_type<op::v1::Gather>(f);
    EXPECT_EQ(gather_ops.size(), 0);
}

TEST(algebraic_simplification, gather_shapeof)
{
    auto check_usecase = [](const Shape& shape,
                            bool is_scalar_index,
                            const std::vector<int64_t>& indices_val,
                            int64_t axis_val) {
        auto indices = is_scalar_index
                           ? op::Constant::create<int64_t>(element::i64, Shape{}, indices_val)
                           : op::Constant::create<int64_t>(
                                 element::i64, Shape{indices_val.size()}, indices_val);
        auto axis = op::Constant::create<int64_t>(element::i64, Shape{}, {axis_val});
        auto A = make_shared<op::Parameter>(element::f32, shape);
        auto A1 = make_shared<op::v0::Abs>(A);
        auto B = make_shared<op::v1::Gather>(A1, indices, axis);
        auto B1 = make_shared<op::v3::ShapeOf>(B);
        auto baseline_f = make_shared<Function>(make_shared<op::v0::Abs>(B1), ParameterVector{A});
        auto optimized_f = clone_function(*baseline_f);
        EXPECT_TRUE((compare_pass_int<pass::AlgebraicSimplification, float, int64_t>(baseline_f,
                                                                                     optimized_f)));

        ASSERT_EQ(count_ops_of_type<op::v3::ShapeOf>(baseline_f), 1);
        ASSERT_EQ(count_ops_of_type<op::v1::Gather>(baseline_f), 1);
        if (is_scalar_index)
        {
            ASSERT_EQ(count_ops_of_type<op::v3::ShapeOf>(optimized_f), 1);
            ASSERT_EQ(count_ops_of_type<op::v1::Gather>(optimized_f), 1);
        }
        else
        {
            ASSERT_EQ(count_ops_of_type<op::v0::Concat>(optimized_f), 1);
        }
    };

    check_usecase(Shape{2, 3, 2, 1}, true, std::vector<int64_t>{0}, 0);
    check_usecase(Shape{2, 3, 2, 1}, true, std::vector<int64_t>{0}, 3);
    check_usecase(Shape{3, 4}, true, std::vector<int64_t>{3}, 1);
    check_usecase(Shape{12}, true, std::vector<int64_t>{0}, 0);
    check_usecase(Shape{2, 3, 2, 1}, false, std::vector<int64_t>{0, 2}, 1);
    check_usecase(Shape{2, 3, 2, 1}, false, std::vector<int64_t>{0}, 2);
}

TEST(algebraic_simplification, dyn_gather_shapeof)
{
    auto check_usecase = [](std::shared_ptr<op::Parameter> data,
                            std::shared_ptr<op::Parameter> indices,
                            int64_t axis_val,
                            bool is_scalar_index) {
        auto axis = op::Constant::create<int64_t>(element::i64, Shape{}, {axis_val});
        auto A1 = make_shared<op::v0::Abs>(data);
        auto B = make_shared<op::v1::Gather>(A1, indices, axis);
        auto B1 = make_shared<op::v3::ShapeOf>(B);
        auto baseline_f =
            make_shared<Function>(make_shared<op::v0::Abs>(B1), ParameterVector{data, indices});
        auto optimized_f = clone_function(*baseline_f);
        pass::Manager pass_manager;
        pass_manager.register_pass<pass::Validate>();
        pass_manager.register_pass<pass::AlgebraicSimplification>();
        pass_manager.run_passes(optimized_f);

        ASSERT_EQ(baseline_f->get_results()[0]->get_output_partial_shape(0),
                  optimized_f->get_results()[0]->get_output_partial_shape(0));

        ASSERT_EQ(count_ops_of_type<op::v3::ShapeOf>(baseline_f), 1);
        ASSERT_EQ(count_ops_of_type<op::v1::Gather>(baseline_f), 1);
        if (is_scalar_index)
        {
            ASSERT_EQ(count_ops_of_type<op::v3::ShapeOf>(optimized_f), 1);
            ASSERT_EQ(count_ops_of_type<op::v1::Gather>(optimized_f), 1);
        }
        else
        {
            ASSERT_EQ(count_ops_of_type<op::v0::Concat>(optimized_f), 1);
        }
    };

    check_usecase(make_shared<op::Parameter>(element::f32, PartialShape{2, 3, -1}),
                  make_shared<op::Parameter>(element::f32, PartialShape{0, 1}),
                  0,
                  false);
    check_usecase(
        make_shared<op::Parameter>(element::f32, PartialShape{Dimension::dynamic(), 3, -1}),
        make_shared<op::Parameter>(element::f32,
                                   PartialShape{Dimension::dynamic(), Dimension::dynamic()}),
        0,
        false);
}
