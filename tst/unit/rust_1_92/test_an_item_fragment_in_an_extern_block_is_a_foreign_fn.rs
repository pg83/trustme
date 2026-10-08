// An `item` fragment put into an `extern` block is a foreign item there:
// rustc's `parse_foreign_item` turns the parsed `ItemKind::Fn` into a
// `ForeignItemKind::Fn`, which takes the block's ABI and is unsafe to call
// (`lower_foreign_item`, `compute_sig_of_foreign_fn_decl`). ring declares its
// C functions with `prefixed_extern!` and keeps them as
// `unsafe extern "C" fn` pointers in its curve tables.
macro_rules! foreign {
    ($name:ident { $item:item }) => {
        extern "C" {
            #[link_name = stringify!($name)]
            $item
        }
    };
}

#[no_mangle]
pub extern "C" fn trustme_twice(x: u32) -> u32 {
    x * 2
}

foreign! { trustme_twice { fn twice(x: u32) -> u32; } }

struct Ops {
    op: unsafe extern "C" fn(u32) -> u32,
}

static OPS: Ops = Ops { op: twice };

fn main() {
    assert_eq!(unsafe { (OPS.op)(21) }, 42);
    let f: unsafe extern "C" fn(u32) -> u32 = twice;
    assert_eq!(unsafe { f(4) }, 8);
    assert_eq!(unsafe { twice(5) }, 10);
}
