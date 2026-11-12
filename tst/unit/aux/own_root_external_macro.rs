#![feature(decl_macro, rustc_attrs)]

#[rustc_macro_transparency = "transparent"]
pub macro verify_capture() {
    {
        let captured = 17u16;
        macro earlier() { 0u16 }
        macro reader() { captured }
        let captured = 23u16;
        assert_eq!(reader!(), 17);
        assert_eq!(captured, 23);
    }
}

#[rustc_macro_transparency = "transparent"]
pub macro verify_rule_capture() {
    {
        let captured = 31u16;
        macro_rules! earlier { () => { 0u16 }; }
        macro_rules! reader { () => { captured }; }
        let captured = 47u16;
        assert_eq!(reader!(), 31);
        assert_eq!(captured, 47);
    }
}
