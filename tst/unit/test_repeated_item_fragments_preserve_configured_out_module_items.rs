macro_rules! repeat_disabled {
    ($item:item) => {
        $item
        $item
        const VALUE_AFTER_DISABLED_ITEMS: u16 = 83;
    };
}

repeat_disabled! {
    #[cfg(any())]
    mod intentionally_absent_module;
}

fn main() {
    assert_eq!(VALUE_AFTER_DISABLED_ITEMS, 83);
}
