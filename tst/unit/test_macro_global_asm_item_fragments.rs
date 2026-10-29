macro_rules! twice {
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

twice! {
    pub mod assembly {
        std::arch::global_asm!(
            ".ifndef cloned_asm_bridge",
            ".globl cloned_asm_bridge",
            ".type cloned_asm_bridge,@function",
            "cloned_asm_bridge:",
            "add rdi, {offset}",
            "jmp {destination}",
            ".endif",
            offset = const 7,
            destination = sym crate::target,
        );

        pub fn check() {
            assert_eq!(unsafe { crate::cloned_asm_bridge(2) }, 18);
        }
    }
}

extern "C" {
    fn cloned_asm_bridge(value: usize) -> usize;
}

fn main() {
    first::assembly::check();
    second::assembly::check();
    assert_eq!(unsafe { cloned_asm_bridge(10) }, 34);
    assert_eq!(unsafe { cloned_asm_bridge(2) }, 18);
}
