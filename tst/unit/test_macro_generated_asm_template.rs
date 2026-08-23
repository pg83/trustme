//@ crate-type: lib

macro_rules! cfg_32 {
    ($($tokens:tt)+) => {
        #[cfg(target_pointer_width = "32")]
        $($tokens)+
    };
}

macro_rules! cfg_64 {
    ($($tokens:tt)+) => {
        #[cfg(target_pointer_width = "64")]
        $($tokens)+
    };
}

macro_rules! define_template {
    ($on_32:item $on_64:item) => {
        cfg_32!($on_32);
        cfg_64!($on_64);
    };
}

pub fn generated_asm_template() {
    define_template!(
        macro_rules! template {
            () => {
                "nop"
            };
        }
        macro_rules! template {
            () => {
                "nop"
            };
        }
    );
    unsafe {
        core::arch::asm!(template!(), options(nomem, nostack));
    }
}
