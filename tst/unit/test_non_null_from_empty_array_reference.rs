//@ crate-type: lib

use std::ptr::NonNull;

fn accepted() {
    let _empty: NonNull<[u8]> = NonNull::from(&mut []);
}
