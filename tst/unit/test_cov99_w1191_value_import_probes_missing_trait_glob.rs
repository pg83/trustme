//@ compile-fail: Unknown associated item Missing of trait
//@ compile-flags: --emit=metadata -Cpanic=abort

#![feature(import_trait_associated_functions)]

trait Catalog {}

fn marker() {}

use crate::marker as imported_marker;
use crate::Catalog::Missing::*;

fn main() {}
