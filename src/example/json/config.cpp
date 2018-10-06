// Copyright (c) 2018 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/json/

#include <tao/json/external/pegtl.hpp>
#include <tao/json/external/pegtl/contrib/parse_tree.hpp>
#include <tao/json/jaxn/from_input.hpp>
#include <tao/json/jaxn/internal/grammar.hpp>
#include <tao/json/stream.hpp>
#include <tao/json/value.hpp>

#include <iomanip>
#include <iostream>

using namespace tao;         // NOLINT
using namespace json_pegtl;  // NOLINT

namespace config
{
   namespace jaxn = json::jaxn::internal::rules;

   // clang-format off
   namespace rules
   {
      using rws = plus< jaxn::ws >;

      struct identifier : plus< ranges< 'a', 'z', 'A', 'Z', '0', '9', '-', '-', '_', '_', '$' > > {};
      struct name_separator : pad< one< ':', '=' >, jaxn::ws > {};
      struct value;

      struct function_param : if_must< identifier, name_separator, value > {};
      struct function : seq< identifier, rws, list< function_param, jaxn::value_separator > > {};

      struct expression;

      struct string_fragment : sor< jaxn::string_fragment, expression > {};
      struct string : list_must< string_fragment, jaxn::value_concat > {};

      struct binary_fragment : sor< expression, jaxn::bvalue > {};
      struct binary : list_must< binary_fragment, jaxn::value_concat > {};

      struct element;
      struct array_content : opt< list_tail< element, jaxn::element_separator > > {};
      struct array_value : seq< jaxn::begin_array, array_content, must< jaxn::end_array > >
      {
         using begin = jaxn::begin_array;
         using end = jaxn::end_array;
         using element = rules::element;
         using content = array_content;
      };
      struct array_fragment : sor< expression, array_value > {};
      struct array : list_must< array_fragment, jaxn::value_concat > {};

      struct mkey_part : sor< identifier, jaxn::string > {};
      struct mkey : list< mkey_part, one< '.' > > {};
      struct member : if_must< mkey, name_separator, value > {};
      struct object_content : star< member, opt< jaxn::value_separator > > {};
      struct object_value : seq< jaxn::begin_object, object_content, must< jaxn::end_object > >
      {
         using begin = jaxn::begin_object;
         using end = jaxn::end_object;
         using element = member;
         using content = object_content;
      };
      struct object_fragment : sor< expression, object_value > {};
      struct object : list_must< object_fragment, jaxn::value_concat > {};

      struct rkey_part : sor< string, identifier > {};
      struct rkey : list< rkey_part, one< '.' > > {};

      struct expression : if_must< json_pegtl::string< '$', '(' >, sor< function, rkey >, one< ')' > > {};
      struct expression_list : seq< expression, star< jaxn::value_concat, sor< expression, must< sor< string, binary, object, array > > > > > {};

      struct sor_value : jaxn::sor_value
      {
         using analyze_t = typename sor< object,
                                         array,
                                         jaxn::null,
                                         jaxn::true_,
                                         jaxn::false_,
                                         string,
                                         expression_list,
                                         binary,
                                         jaxn::number< true >,
                                         jaxn::number< false > >::analyze_t;

         template< apply_mode A,
                   rewind_mode M,
                   template< typename... > class Action,
                   template< typename... > class Control,
                   typename Input,
                   typename... States >
         static bool match_impl( Input& in, States&&... st )
         {
            switch( in.peek_char() ) {
               case '{': return Control< object >::template match< A, M, Action, Control >( in, st... );
               case '[': return Control< array >::template match< A, M, Action, Control >( in, st... );
               case 'n': return Control< jaxn::null >::template match< A, M, Action, Control >( in, st... );
               case 't': return Control< jaxn::true_ >::template match< A, M, Action, Control >( in, st... );
               case 'f': return Control< jaxn::false_ >::template match< A, M, Action, Control >( in, st... );

               case '"':
               case '\'':
                  return Control< string >::template match< A, M, Action, Control >( in, st... );

               case '$':
                  if( in.peek_char( 1 ) == '(' ) {
                     return Control< expression_list >::template match< A, M, Action, Control >( in, st... );
                  }
                  else {
                     return Control< binary >::template match< A, M, Action, Control >( in, st... );
                  }

               case '+':
                  in.bump_in_this_line();
                  if( in.empty() || !match_number< false, A, rewind_mode::DONTCARE, Action, Control >( in, st... ) ) {
                     throw parse_error( "incomplete number", in );
                  }
                  return true;

               case '-':
                  in.bump_in_this_line();
                  if( in.empty() || !match_number< true, A, rewind_mode::DONTCARE, Action, Control >( in, st... ) ) {
                     throw parse_error( "incomplete number", in );
                  }
                  return true;

               default:
                  return match_number< false, A, M, Action, Control >( in, st... );
            }
         }

         template< apply_mode A,
                   rewind_mode M,
                   template< typename... > class Action,
                   template< typename... > class Control,
                   typename Input,
                   typename... States >
         static bool match( Input& in, States&&... st )
         {
            if( in.size( 2 ) && match_impl< A, M, Action, Control >( in, st... ) ) {
               in.discard();
               return true;
            }
            return false;
         }
      };

      struct value : jaxn::padr< sor_value > {};
      struct element : value {};

      struct kw_include : TAO_JSON_PEGTL_STRING( "include" ) {};
      struct kw_delete : TAO_JSON_PEGTL_STRING( "delete" ) {};

      struct include_file : seq< kw_include, rws, string > {};
      struct delete_keys : seq< kw_delete, rws, mkey > {};

      struct statement : sor< include_file, delete_keys, member > {};

      struct grammar : until< eof, sor< jaxn::ws, must< rules::statement > > > {};
      // clang-format on

   }  // namespace rules

   template< typename Rule >
   using selector = parse_tree::selector<
      Rule,
      parse_tree::apply_remove_content::to<
         rules::include_file,
         rules::delete_keys,
         rules::member,
         rules::string,
         rules::array,
         rules::object,
         rules::function,
         rules::function_param,
         rules::expression,
         rules::rkey,
         rules::binary,
         rules::mkey,
         rules::element,
         jaxn::infinity< true >,
         jaxn::infinity< false >,
         jaxn::null,
         jaxn::true_,
         jaxn::false_,
         jaxn::nan >,
      parse_tree::apply_store_content::to<
         rules::identifier,
         jaxn::bvalue,
         jaxn::number< true >,
         jaxn::number< false >,
         jaxn::hexnum< true >,
         jaxn::hexnum< false >,
         jaxn::string,
         jaxn::string_fragment > >;

   void print( const parse_tree::node& n, const std::string& s = "" )
   {
      // detect the root node:
      if( n.is_root() ) {
         std::cout << "ROOT" << std::endl;
      }
      else {
         if( n.has_content() ) {
            std::cout << s << n.name() << " \"" << n.content() << "\" at " << n.begin() << " to " << n.end() << std::endl;
         }
         else {
            std::cout << s << n.name() << " at " << n.begin() << std::endl;
         }
      }

      // print all child nodes
      if( !n.children.empty() ) {
         const auto s2 = s + "  ";
         for( const auto& up : n.children ) {
            print( *up, s2 );
         }
      }
   }

   using node_map = std::map< json::pointer, std::shared_ptr< parse_tree::node > >;

   json::pointer key( const parse_tree::node& n )
   {
      json::pointer result;
      for( const auto& p : n.children ) {
         if( p->is< config::rules::identifier >() ) {
            result.push_back( p->content() );
         }
         else if( p->is< jaxn::string >() ) {
            result.push_back( json::jaxn::from_input( p->as_memory_input() ).get_string() );
         }
         else {
            throw std::logic_error( "code should be unreachable" );  // NOLINT, LCOV_EXCL_LINE
         }
      }
      return result;
   }

   void add( node_map& m, parse_tree::node& n )
   {
      assert( n.children.size() == 2 );
      m.emplace( key( *n.children.front() ), std::move( n.children.back() ) );
   }

   node_map process( parse_tree::node& n )
   {
      node_map result;
      for( auto& e : n.children ) {
         if( e->is< config::rules::member >() ) {
            add( result, *e );
         }
         else if( e->is< config::rules::delete_keys >() ) {
            // TODO: Implement me!
         }
         else if( e->is< config::rules::include_file >() ) {
            // TODO: Implement me!
         }
         else {
            throw std::logic_error( "code should be unreachable" );  // NOLINT, LCOV_EXCL_LINE
         }
      }
      return result;
   }

}  // namespace config

int main( int argc, char** argv )
{
   for( int i = 1; i < argc; ++i ) {
      file_input<> in( argv[ i ] );
      try {
         const auto root = parse_tree::parse< config::rules::grammar, config::selector >( in );
         config::print( *root );
         const auto result = config::process( *root );
         for( const auto& e : result ) {
            std::cout << '"' << to_string( e.first ) << '"' << std::endl;
         }
      }
      catch( const parse_error& e ) {
         const auto p = e.positions.front();
         std::cerr << e.what() << std::endl
                   << in.line_as_string( p ) << std::endl
                   << std::string( p.byte_in_line, ' ' ) << '^' << std::endl;
      }
   }
   return 0;
}
