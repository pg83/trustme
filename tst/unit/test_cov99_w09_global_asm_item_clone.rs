// The unexpanded invocation is cloned before global_asm produces an AST item.
macro_rules! copy_module {
    ($item:item) => {
        mod first { $item }
        mod second { $item }
    };
}

copy_module! {
    pub mod payload {
        core::arch::global_asm!("");
        pub const VALUE: u32 = 42;
    }
}

macro_rules! copy_block {
    ($block:block) => { ($block, $block) };
}

fn main() {
    let values = copy_block!({
        core::arch::global_asm!("");
        42u32
    });
    assert_eq!(first::payload::VALUE, values.0);
    assert_eq!(second::payload::VALUE, values.1);
}
