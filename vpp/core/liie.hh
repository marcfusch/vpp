#pragma once

#include <iod/sio.hh>
#include <iod/grammar.hh>
#include <iod/foreach.hh>
#include <iod/symbols.hh>

#include <iod/grammar_utils.hh>
#include <iod/apply.hh>
#include <iod/callable_traits.hh>

namespace vpp
{

  using s::_Sum; using s::_Sum_t;
  using s::_Avg; using s::_Avg_t;
  using s::_Min; using s::_Min_t;
  using s::_Max; using s::_Max_t;
  using s::_Argmax; using s::_Argmax_t;
  using s::_Argmin; using s::_Argmin_t;
  using s::_If; using s::_If_t;
  using s::_V; using s::_V_t;

  using s::_1; using s::_2;
  
  namespace liie
  {
    struct empty_visitor
    {
    };

    template <typename Test, typename Then>
    using if_then_exp = iod::function_call_exp<iod::function_call_exp<_If_t, Test>, Then>;

    template <typename Test, typename Then, typename Else>
    using if_then_else_exp = iod::function_call_exp<iod::function_call_exp<
                                                      iod::function_call_exp<
                                                        _If_t, Test>, Then>, Else>;

    // Evaluate an expression against a context.
    struct evaluate_visitor
    {
      template <typename V, typename T, typename E, typename M, typename C>
      inline auto operator()(const if_then_else_exp<V, T, E>& e, M eval, C& ctx) const
      {
        auto _test = std::get<0>(e.method.method.args);
        auto _then = std::get<0>(e.method.args);
        auto _else = std::get<0>(e.args);

        return iod::exp_evaluate(_test, eval, ctx) ?
          iod::exp_evaluate(_then, eval, ctx) :
          iod::exp_evaluate(_else, eval, ctx);
      }

      template <typename V, typename T, typename M, typename C>
      inline void operator()(const if_then_exp<V, T>& e, M eval, C& ctx) const
      {
        auto _test = std::get<0>(e.method.method.args);
        auto _then = std::get<0>(e.method.args);

        if (iod::exp_evaluate(_test, eval, ctx))
          iod::exp_evaluate(_then, eval, ctx);
      }
      
      template <int N, typename M, typename C>
      inline auto& operator()(iod::int_symbol<N>, M eval, C& ctx) const
      {
        return std::get<N - 1>(ctx);
      }

      template <typename T, typename M, typename C>
      inline auto& operator()(const iod::function_call_exp<_V_t, T>& read, M eval, C& ctx) const
      {
        return iod::exp_evaluate(std::get<0>(read.args), eval, ctx);
      }
      
    };

    // Get the images arguments of an expression.
    struct get_exp_images_ranges_visitor
    {
      template <typename T, typename C>
      auto operator()(image2d<T>& img, C& ctx) const { return std::make_tuple(img); }
      template <typename T, typename C>
      auto operator()(const image2d<T>& img, C& ctx) const { return std::make_tuple(img); }
    };

    struct get_exp_placeholders_ranges_visitor
    {
      template <int N, typename C>
      auto operator()(iod::int_symbol<N>, C& ctx) const { return std::make_tuple(std::get<N - 1>(ctx)); }
    };
    
    struct tuple_exp_reduce
    {
      template <typename... T>
      auto operator()(T... t) { return std::tuple_cat(t...); }
    };

    template <typename E, typename C>
    auto get_exp_ranges(E& exp, C& ctx)
    {
      return std::tuple_cat(ctx,
                            iod::exp_map_reduce(exp, std::tuple<>(), ctx,
                                                get_exp_images_ranges_visitor(),
                                                tuple_exp_reduce()));
    }

    // Count the number of placeholders in an expression.
    struct count_placeholders_visitor
    {
      template <int N, int S>
      auto operator()(iod::int_symbol<N>& n, std::integral_constant<int, S>)
      { return make_pair(n, std::integral_constant<int, N + 1>()); }
    };

    template <typename T>
    auto count_exp_placeholders(T& node)
    {
      auto res = iod::exp_transform_iterate(node, count_placeholders_visitor(),
                                            std::integral_constant<int, 0>());
      return res.second;
    }

    // images_to_tuple_accessors;
    //   Transform an ast such as ranges are replaced with int_symbol<X>,
    //   N is the position of the range in the expression numbered from
    //   left to right.
    //   If the expression already contains _1, ..., _N placeholders,
    //   X starts at _N + 1.
    struct images_to_tuple_accessors_visitor
    {
      template <typename T, int N>
      auto operator()(image2d<T>& node, std::integral_constant<int, N>)
      {
        return make_pair(iod::int_symbol<N>(), std::integral_constant<int, N + 1>());
      }
    };

    template <typename T, int S>
    auto images_to_placeholders(T& node, std::integral_constant<int, S> N)
    {
      auto res = iod::exp_transform_iterate(node, images_to_tuple_accessors_visitor(), N);
      return res.first;
    }

    template <typename P>
    using to_pixel_wise_kernel_argument = decltype(*std::declval<get_row_iterator_t<P>>());

    // Evaluate global expressions such as _Sum, _Avg, _Min, _Max, _Argmin ...
    struct evaluate_global_visitor
    {
      // Helpers
      template <typename... PS>
      static auto ranges_to_pw_args(std::tuple<PS...> ctx)
        -> decltype(std::forward_as_tuple(std::declval<to_pixel_wise_kernel_argument<PS>>()...));

      template <typename E, typename... PS>
      static auto eval_return_type(E e, std::tuple<PS...> ranges)
        -> decltype(evaluate(e, std::declval<decltype(ranges_to_pw_args(ranges))&>()));

      template <typename E, typename... PS>
      static auto exp_return_type(E e, std::tuple<PS...> ranges)
        -> decltype(evaluate(images_to_placeholders(e,
                                                    std::integral_constant<int, 1 + sizeof...(PS)>()),
                             std::declval<decltype(ranges_to_pw_args(get_exp_ranges(e, ranges)))&>()));

      template <typename C, typename A>
      auto args_ctx_to_ranges(const A& args, C& ctx) const
      {
        return iod::foreach(args) | [&] (auto& a) {
          return iod::static_if<iod::is_int_symbol<decltype(a)>::value>(
            [&] (auto& c, auto& a) { return std::get<std::remove_reference_t<decltype(a)>::to_int - 1>(c); },
            [&] (auto& c, auto& a) { return a; },
            ctx, a);
          };
      }


      template <typename E, typename C, typename F>
      inline void run_pixel_wise(E& e, C& c, F f) const
      {
        auto n = images_to_placeholders(e, std::integral_constant<int, 1 + std::tuple_size<C>::value>());
        auto ranges = get_exp_ranges(e, c);
        
        pixel_wise(ranges)(_No_threads, _Tie_arguments) | [&] (auto& tp) {
          f(evaluate(n, tp));
        };
      }


      template <typename E, typename C, typename F>
      inline void run_pixel_wise_with_coordinates(E& e, C& c, F f) const
      {
        auto n = images_to_placeholders(e, std::integral_constant<int, 1 + std::tuple_size<C>::value>());
        auto ranges = get_exp_ranges(e, c);

        auto fr = std::get<0>(ranges);
        box2d domain(fr.first_point_coordinates(), fr.last_point_coordinates());
        auto ranges2 = std::tuple_cat(ranges, std::make_tuple(domain));
        iod::apply(ranges2, pixel_wise)(_Tie_arguments, _No_threads) | [&] (auto tp) {
          f(evaluate(n, tp), std::get<std::tuple_size<decltype(tp)>::value - 1>(tp));
        };
      }

      // Min 2.
      template <typename I, typename... PS>
      inline auto operator()(const iod::function_call_exp<_Min_t, I>& n, std::tuple<PS...>& ctx) const
      {
        typedef decltype(exp_return_type(std::get<0>(n.args), ctx)) min_type;
        min_type min_value = std::numeric_limits<min_type>::max();
        
        run_pixel_wise(std::get<0>(n.args), ctx,
                       [&] (auto&& v) { min_value = std::min(min_value, v); });

        return min_value;
      }
      
      // Max.
      template <typename I, typename... PS>
      inline auto operator()(const iod::function_call_exp<_Max_t, I>& n, std::tuple<PS...>& ctx) const
      {
        typedef decltype(exp_return_type(std::get<0>(n.args), ctx)) max_type;
        max_type max_value = std::numeric_limits<max_type>::min();
        
        run_pixel_wise(std::get<0>(n.args), ctx,
                       [&] (auto&& v) { max_value = std::max(max_value, v); });

        return max_value;
      }

      // Sum.
      template <typename I, typename... PS>
      inline auto operator()(const iod::function_call_exp<_Sum_t, I>& n, std::tuple<PS...>& ctx) const
      {
        typedef decltype(exp_return_type(std::get<0>(n.args), ctx)) elt_type;
        typedef decltype(elt_type() + elt_type()) sum_type;
        sum_type sum = zero<sum_type>();
        
        run_pixel_wise(std::get<0>(n.args), ctx,
                       [&] (auto&& v) { sum += v; });
        return sum;
      }

      // Avg.
      template <typename I, typename... PS>
      inline auto operator()(const iod::function_call_exp<_Avg_t, I>& n, std::tuple<PS...>& ctx) const
      {
        typedef decltype(exp_return_type(std::get<0>(n.args), ctx)) elt_type;
        typedef decltype(elt_type() + elt_type()) sum_type;
        sum_type sum = zero<sum_type>();
        int i = 0;
        run_pixel_wise(std::get<0>(n.args), ctx,
                       [&] (auto&& v) { sum += v; i++; });
        return sum / float(i);
      }

      // Argmin.
      template <typename I, typename... PS>
      inline auto operator()(iod::function_call_exp<_Argmin_t, I>& n, std::tuple<PS...>& ctx) const
      {
        typedef decltype(exp_return_type(std::get<0>(n.args), ctx)) min_type;
        min_type min_value = std::numeric_limits<min_type>::max();
        vint2 min_p(0,0);
        
        run_pixel_wise_with_coordinates(std::get<0>(n.args), ctx, [&] (auto&& v, vint2 p) {
            if (v < min_value)
            {
              min_value = v; min_p = p;
            }
          });

        return min_p;
      }

      // Argmax.
      template <typename I, typename... PS>
      inline auto operator()(iod::function_call_exp<_Argmax_t, I>& n, std::tuple<PS...>& ctx) const
      {
        typedef decltype(exp_return_type(std::get<0>(n.args), ctx)) max_type;
        max_type max_value = std::numeric_limits<max_type>::min();
        vint2 max_p(0,0);
        
        run_pixel_wise_with_coordinates(std::get<0>(n.args), ctx, [&] (auto&& v, vint2 p) {
            if (v > max_value)
            {
              max_value = v; max_p = p;
            }
          });

        return max_p;
      }
      
    };
    
    template <typename E, typename C>
    auto evaluate_global_expressions(E exp, C&& ctx)
    {
      auto e1 = iod::exp_transform(exp, evaluate_global_visitor(), ctx);
      return iod::exp_evaluate(e1, empty_visitor(), ctx);
    }


    template <typename E, typename C>
    auto evaluate(E _exp, C& _ctx)
    {
      auto exp = images_to_placeholders(_exp, std::integral_constant<int, 1 + std::tuple_size<C>::value>());
      auto ctx = get_exp_ranges(exp, _ctx);
      return iod::exp_evaluate(exp, evaluate_visitor(), ctx);
    }
    
    // auto eval(E&& exp)
    // {
    // }
  }

  template <typename... PS, typename E>
  auto eval(PS&&... params, E&& exp)
  {
    auto tp = std::forward_as_tuple(params...);
    auto x = liie::evaluate_global_expressions(exp, tp);

    
    return iod::static_if<std::is_base_of<iod::Exp<decltype(x)>, decltype(x)>::value>(
      [&] (auto& x, auto& tp) {
        auto x2 = liie::images_to_placeholders(x, std::integral_constant<int, 1 + std::tuple_size<std::remove_reference_t<decltype(tp)>>::value>());
        auto ctx = liie::get_exp_ranges(x, tp);
        return pixel_wise(ctx).run_exp(x2);
      },
      [] (auto& x, auto& tp) { return x; },
      x, tp);
  }

  // template <typename E, typename C>
  // auto evaluate(E _exp, C& _ctx)
  // {
  //   auto exp = images_to_placeholders(_exp, std::integral_constant<int, 1 + std::tuple_size<C>::value>());
  //   auto ctx = get_exp_ranges(exp, _ctx);
  //   return iod::exp_evaluate(exp, evaluate_visitor(), ctx);
  // }
  
}
