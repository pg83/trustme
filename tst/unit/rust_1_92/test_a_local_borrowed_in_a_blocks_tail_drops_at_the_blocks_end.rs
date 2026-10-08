// pin-project's `weird_repr_packed` takes `{ let x = Struct { .. };
// &x.field as *const u8 as usize }` and expects `x`'s `Drop` to have run
// when the block ends. rustc extends the scope of temporaries under a `&`
// in an extending position of a `let` initializer, the `as` casts and the
// block's tail included (`record_rvalue_scope_if_borrow_expr`), but only
// temporaries: `x` is a variable, and it is dropped at the end of its
// block. We moved the variable's drop to the scope of the outer `let`.
static mut DROPPED: u32 = 0;

struct Plain {
    field: u8,
}

impl Drop for Plain {
    fn drop(&mut self) {
        unsafe {
            DROPPED += 1;
        }
    }
}

#[repr(packed)]
struct Packed {
    field: u8,
}

impl Drop for Packed {
    fn drop(&mut self) {
        unsafe {
            DROPPED += 10;
        }
    }
}

fn dropped() -> u32 {
    unsafe { DROPPED }
}

fn main() {
    let address = {
        let x = Plain { field: 27 };
        &x.field as *const u8 as usize
    };
    assert_eq!(dropped(), 1);
    let pointer = {
        let x = Packed { field: 27 };
        &x.field as *const u8
    };
    assert_eq!(dropped(), 11);
    assert!(address != 0 && !pointer.is_null());
}
