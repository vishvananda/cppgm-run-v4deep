# Per-tool implementation source lists for the compiler.
#
# Add dev/src/foo.cpp to the tools that use it by adding `foo` below. For
# subdirectories, use the path without `.cpp`, such as `parser/foo`.

FRONTEND_SOURCE_SET_TARGETS := abimangle pptoken posttoken ppexpr preproc cppgm++ lowiropt lowir lowir2native
FRONTEND_TEST_RUNNER_SOURCE_ID := support/testing/test_runner

FRONTEND_OBJ_BASENAMES_abimangle :=
FRONTEND_OBJ_BASENAMES_pptoken := preprocess/tokens/pp_source_translation preprocess/tokens/pp_tokenizer
FRONTEND_OBJ_BASENAMES_posttoken := posttoken/fundamental_type posttoken/simple_token posttoken/pa2_decode posttoken/post_literal posttoken/pp_number posttoken/post_token_stream preprocess/tokens/pp_source_translation preprocess/tokens/pp_tokenizer
FRONTEND_OBJ_BASENAMES_ppexpr := preprocess/ctrl_expr/ctrl_expr_parser preprocess/ctrl_expr/ctrl_expr_stream posttoken/fundamental_type posttoken/pa2_decode posttoken/post_literal posttoken/pp_number posttoken/simple_token posttoken/post_token_stream preprocess/tokens/pp_source_translation preprocess/tokens/pp_tokenizer
FRONTEND_OBJ_BASENAMES_preproc := preprocess/ctrl_expr/ctrl_expr_parser preprocess/ctrl_expr/ctrl_expr_stream preprocess/preproc/pp_expander preprocess/preproc/pp_file_identity preprocess/preproc/pp_macro preprocess/preproc/pp_preprocessor preprocess/preproc/pp_token preprocess/preproc/pp_token_reader preprocess/tokens/pp_source_translation preprocess/tokens/pp_tokenizer posttoken/fundamental_type posttoken/pa2_decode posttoken/post_literal posttoken/pp_number posttoken/post_token_stream posttoken/simple_token
FRONTEND_OBJ_BASENAMES_cppgm++ := semantic/semantic_analyzer semantic/semantic_analyzer_class semantic/semantic_analyzer_decl semantic/semantic_constant semantic/semantic_driver semantic/semantic_dump semantic/semantic_expression semantic/semantic_model semantic/semantic_semantics semantic/semantic_template semantic/semantics_dump syntax/syntax_arena syntax/syntax_driver syntax/syntax_token syntax/syntax_parser syntax/syntax_parser_stmt syntax/syntax_parser_type preprocess/ctrl_expr/ctrl_expr_parser preprocess/ctrl_expr/ctrl_expr_stream preprocess/preproc/pp_expander preprocess/preproc/pp_file_identity preprocess/preproc/pp_macro preprocess/preproc/pp_preprocessor preprocess/preproc/pp_token preprocess/preproc/pp_token_reader preprocess/tokens/pp_source_translation preprocess/tokens/pp_tokenizer posttoken/fundamental_type posttoken/pa2_decode posttoken/post_literal posttoken/pp_number posttoken/post_token_stream posttoken/simple_token
FRONTEND_OBJ_BASENAMES_lowiropt :=
FRONTEND_OBJ_BASENAMES_lowir :=
FRONTEND_OBJ_BASENAMES_lowir2native :=
