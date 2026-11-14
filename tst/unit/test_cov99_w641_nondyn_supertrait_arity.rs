//@ compile-fail: Incorrect number of arguments
//@ compile-flags: --emit=metadata -Cpanic=abort
//@ aux-build: cov99_w641_route_previous/cov99_w641_route.rs
//@ aux-build: cov99_w641_route_derived.rs
//@ aux-build: cov99_w641_route_current/cov99_w641_route.rs
//@ aux-output-dir: cov99_w641_route_previous/cov99_w641_route.rs=previous
//@ aux-output-dir: cov99_w641_route_current/cov99_w641_route.rs=current

use cov99_w641_route::Route;
use cov99_w641_route_derived::Derived;

fn probe<T: Derived + Route<()>>(value: &T) -> u8 {
    value.route(1u8)
}

fn main() {}
