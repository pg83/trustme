// rustc parses `static NAME: TYPE;` and `static mut NAME: TYPE;` with no value
// as items: `parse_static_item` (rustc_parse/src/parser/item.rs) makes the
// `= expr` optional, and AST validation rejects a free static without a body
// later. So an `item` fragment matches one, and a macro can put it into an
// `extern` block. ring's `prefixed_extern!` declares
// `static avx2_available: AtomicU32;` this way.
macro_rules! prefixed_item {
    ($name:ident { $item:item }) => {
        #[link_name = stringify!($name)]
        $item
    };
}

macro_rules! foreign_static {
    ($vis:vis static mut $name:ident: $ty:ty;) => {
        extern "C" {
            prefixed_item! { trustme_counter { $vis static mut $name: $ty; } }
        }
    };
    ($vis:vis static $name:ident: $ty:ty;) => {
        extern "C" {
            prefixed_item! { trustme_answer { $vis static $name: $ty; } }
        }
    };
}

#[no_mangle]
pub static trustme_answer: u32 = 42;
#[no_mangle]
pub static mut trustme_counter: u32 = 7;

foreign_static! { static ANSWER: u32; }
foreign_static! { pub static mut COUNTER: u32; }

fn main() {
    assert_eq!(unsafe { ANSWER }, 42);
    unsafe {
        COUNTER += 1;
    }
    assert_eq!(unsafe { COUNTER }, 8);
}
