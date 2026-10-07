pub use item_head_text::item_head;

#[macro_export]
macro_rules! headed {
    ($item:item) => {
        #[cfg_attr(any(), $crate::item_head)]
        #[$crate::item_head]
        $item
    };
}
