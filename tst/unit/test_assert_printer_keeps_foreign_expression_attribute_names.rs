//@ proc-macro-aux-build: own_expression_attribute_identity.rs
//@ aux-build: own_qualified_expression_attribute.rs
#![feature(stmt_expr_attributes, proc_macro_hygiene)]

fn main() {
    own_qualified_expression_attribute::check!();
}
