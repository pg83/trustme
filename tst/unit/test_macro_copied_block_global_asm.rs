macro_rules! duplicate {
    ($item:item) => {
        mod first { $item }
        macro_rules! later {
            () => { mod second { $item } };
        }
        later!();
    };
}

extern "C" fn target(value: usize) -> usize {
    value * 2
}

duplicate! {
    pub mod assembly {
        pub fn check() {
            mod local {
                std::arch::global_asm!(
                    ".ifndef copied_block_bridge",
                    ".globl copied_block_bridge",
                    ".type copied_block_bridge,@function",
                    "copied_block_bridge:",
                    "add rdi, {offset}",
                    "jmp {destination}",
                    ".endif",
                    offset = const 7,
                    destination = sym crate::target,
                );
            }
            assert_eq!(unsafe { crate::copied_block_bridge(2) }, 18);
        }
    }
}

extern "C" {
    fn copied_block_bridge(value: usize) -> usize;
}

fn main() {
    first::assembly::check();
    second::assembly::check();
    assert_eq!(unsafe { copied_block_bridge(10) }, 34);
}
