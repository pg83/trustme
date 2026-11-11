#![feature(linkage, thread_local)]
#![allow(internal_features)]

#[thread_local]
#[linkage = "external"]
#[export_name = "trustme_literal_tls_counter"]
static mut COUNTER: u32 = 11;

#[export_name = "trustme_literal_alias_add"]
extern "C" fn add_impl(value: u32) -> u32 {
    value + 5
}

extern "C" {
    #[thread_local]
    #[link_name = "\x01trustme_literal_tls_counter"]
    static mut COUNTER_ALIAS: u32;

    #[link_name = "\x01trustme_literal_alias_add"]
    fn add_alias(value: u32) -> u32;
}

fn main() {
    unsafe {
        assert_eq!(std::ptr::addr_of!(COUNTER), std::ptr::addr_of!(COUNTER_ALIAS));
        assert_eq!(std::ptr::read_volatile(std::ptr::addr_of!(COUNTER_ALIAS)), 11);
        std::ptr::write_volatile(std::ptr::addr_of_mut!(COUNTER), 29);
        assert_eq!(std::ptr::read_volatile(std::ptr::addr_of!(COUNTER_ALIAS)), 29);
        std::ptr::write_volatile(std::ptr::addr_of_mut!(COUNTER_ALIAS), 41);
        assert_eq!(std::ptr::read_volatile(std::ptr::addr_of!(COUNTER)), 41);
        assert_eq!(add_alias(17), 22);

        let address: usize;
        std::arch::asm!(
            "lea {address}, [rip + {function}]",
            address = out(reg) address,
            function = sym add_alias,
            options(nomem, nostack, preserves_flags),
        );
        assert_eq!(address, add_alias as *const () as usize);
    }
}
