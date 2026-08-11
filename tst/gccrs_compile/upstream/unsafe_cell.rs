// { dg-additional-options "-fdump-tree-gimple" }
use std::cell::UnsafeCell;
pub fn normal_ref(_value: &i32) {}
pub fn unsafe_ref(_value: &UnsafeCell<i32>) {}
