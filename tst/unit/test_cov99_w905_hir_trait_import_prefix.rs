//@ edition: 2021
//@ compile-fail: Trait associated imports require #![feature(import_trait_associated_functions)]

use core::marker::Copy as ForeignTrait;
use ForeignTrait::missing;

fn main() {}
