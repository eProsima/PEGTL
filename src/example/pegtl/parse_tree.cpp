// Copyright (c) 2017-2020 Dr. Colin Hirsch and Daniel Frey
// Please see LICENSE for license or visit https://github.com/taocpp/PEGTL/

#include <iostream>
#include <string>
#include <type_traits>

#include <tao/pegtl.hpp>
#include <tao/pegtl/contrib/parse_tree.hpp>
#include <tao/pegtl/contrib/parse_tree_to_dot.hpp>

using namespace tao::TAO_PEGTL_NAMESPACE;  // NOLINT

namespace example
{
   // the grammar

   // clang-format off
   // Some basic constructs
   struct sign : one< '+', '-'> {};
   struct integer : plus< digit > {};

   // FIELDNAME
   struct dot_op : one< '.' > {};
   struct fieldname_part : seq< identifier, opt< seq< one<'['>, integer, one<']'> > > > {};
   struct fieldname : list<fieldname_part, dot_op> {};

   // CHARVALUE, STRING, ENUMERATEDVALUE
   struct open_quote : one< '`', '\'' > {};
   struct close_quote : one< '\'' > {};
   struct char_value : seq< open_quote, any, close_quote > {};
   struct string_value : seq< open_quote, star< not_one< '\'', '\r', '\n'> >, close_quote > {};
   struct enumerated_value : seq< open_quote, identifier, close_quote > {};

   // BOOLEANVALUE
   struct false_value : pad< TAO_PEGTL_KEYWORD("FALSE"), space > {};
   struct true_value : pad< TAO_PEGTL_KEYWORD("TRUE"), space > {};
   struct boolean_value : sor<false_value, true_value> {};

   // INTEGERVALUE, FLOATVALUE
   struct integer_value : seq< opt< sign >, integer > {};
   struct fractional : seq< dot_op, integer > {};
   struct exponent : seq< one< 'e', 'E' >, integer_value > {};
   struct float_value : seq< integer_value, opt< fractional >, opt< exponent > > {};

   // PARAMETER
   struct parameter_value : seq< one< '%' >, digit, opt< digit > > {};
   
   // Keyword based operators
   struct and_op : pad< TAO_PEGTL_KEYWORD("AND"), space > {};
   struct or_op : pad< TAO_PEGTL_KEYWORD("OR"), space> {};
   struct not_op : pad< TAO_PEGTL_KEYWORD("NOT"), space> {};
   struct between_op : pad< TAO_PEGTL_KEYWORD("BETWEEN"), space> {};
   struct not_between_op : pad< TAO_PEGTL_KEYWORD("NOT BETWEEN"), space> {};

   // RelOp
   struct eq_op : pad< one<'='>, space> {};
   struct gt_op : pad< one<'>'>, space> {};
   struct ge_op : pad< TAO_PEGTL_KEYWORD(">="), space> {};
   struct lt_op : pad< one<'<'>, space> {};
   struct le_op : pad< TAO_PEGTL_KEYWORD("<="), space> {};
   struct ne_op : pad< sor< TAO_PEGTL_KEYWORD("<>"), TAO_PEGTL_KEYWORD("!=") >, space> {};
   struct like_op : pad< sor< TAO_PEGTL_KEYWORD("LIKE"), TAO_PEGTL_KEYWORD("like") >, space> {};
   struct rel_op : sor< like_op, ne_op, le_op, ge_op, lt_op, gt_op, eq_op > {};
 
   // Parameter, Range
   struct Literal : sor< boolean_value, integer_value, float_value, char_value, enumerated_value, string_value > {};
   struct Parameter : sor< Literal, parameter_value > {};
   struct Range : seq< Parameter, and_op, Parameter > {};

   // Predicates
   struct BetweenPredicate : seq< fieldname, sor< not_between_op, between_op >, Range > {};
   struct ComparisonPredicate : sor<
                                     seq< Parameter, rel_op, fieldname >,
                                     seq< fieldname, rel_op, Parameter >,
                                     seq< fieldname, rel_op, fieldname >
                                   > {};
   struct Predicate : sor< ComparisonPredicate, BetweenPredicate > {};

   // Brackets
   struct open_bracket : seq< one< '(' >, star< space > > {};
   struct close_bracket : seq< star< space >, one< ')' > > {};

   // Condition, FilterExpression
   struct Condition;
   struct ConditionList : list_must< Condition, sor< and_op, or_op > > {};
   struct Condition : sor<
                           Predicate,
                           seq< open_bracket, ConditionList, close_bracket >,
                           seq< not_op, ConditionList >
                         > {};
   struct FilterExpression : ConditionList {};

   // Main grammar
   struct grammar : must< FilterExpression, eof > {};
   // clang-format on

   // after a node is stored successfully, you can add an optional transformer like this:
   struct rearrange
      : parse_tree::apply< rearrange >  // allows bulk selection, see selector<...>
   {
      // recursively rearrange nodes. the basic principle is:
      //
      // from:          PROD/EXPR
      //                /   |   \          (LHS... may be one or more children, followed by OP,)
      //             LHS... OP   RHS       (which is one operator, and RHS, which is a single child)
      //
      // to:               OP
      //                  /  \             (OP now has two children, the original PROD/EXPR and RHS)
      //         PROD/EXPR    RHS          (Note that PROD/EXPR has two fewer children now)
      //             |
      //            LHS...
      //
      // if only one child is left for LHS..., replace the PROD/EXPR with the child directly.
      // otherwise, perform the above transformation, then apply it recursively until LHS...
      // becomes a single child, which then replaces the parent node and the recursion ends.
      template< typename... States >
      static void transform( std::unique_ptr< parse_tree::node >& n, States&&... st )
      {
         if( n->children.size() == 1 ) {
            n = std::move( n->children.back() );
         }
         else {
            n->remove_content();
            auto& c = n->children;
            auto r = std::move( c.back() );
            c.pop_back();
            auto o = std::move( c.back() );
            c.pop_back();
            o->children.emplace_back( std::move( n ) );
            o->children.emplace_back( std::move( r ) );
            n = std::move( o );
            transform( n->children.front(), st... );
         }
      }
   };

   // select which rules in the grammar will produce parse tree nodes:

   template< typename Rule >
   using selector = parse_tree::selector<
      Rule,
      parse_tree::store_content::on<
         boolean_value,
         integer_value,
         float_value,
         char_value,
         enumerated_value,
         string_value,
         parameter_value,
         fieldname_part >,
      parse_tree::remove_content::on<
         eq_op,
         gt_op,
         ge_op,
         lt_op,
         le_op,
         ne_op,
         like_op,
         and_op,
         or_op,
         not_op,
         dot_op,
         between_op,
         not_between_op >,
      rearrange::on< fieldname, ComparisonPredicate, BetweenPredicate, Range, Condition, FilterExpression > >;

}  // namespace example

int main( int argc, char** argv )
{
   if( argc != 2 ) {
      std::cerr << "Usage: " << argv[ 0 ] << " EXPR\n"
                << "Generate a 'dot' file from expression.\n\n"
                << "Example: " << argv[ 0 ] << " \"(2*a + 3*b) / (4*n)\" | dot -Tpng -o parse_tree.png\n";
      return 1;
   }
   argv_input<> in( argv, 1 );
   try {
      const auto root = parse_tree::parse< example::grammar, example::selector >( in );
      if( nullptr == root )
         throw std::exception( "parse returned nullptr" );
      parse_tree::print_dot( std::cout, *root );
      return 0;
   }
   catch( const parse_error& e ) {
      const auto p = e.positions.front();
      std::cerr << e.what() << std::endl
                << in.line_at( p ) << std::endl
                << std::string( p.byte_in_line, ' ' ) << '^' << std::endl;
   }
   catch( const std::exception& e ) {
      std::cerr << e.what() << std::endl;
   }
   return 1;
}
