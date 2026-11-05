//@ reject-flags: --cfg=bad_c_string_pattern || --cfg=bad_c_string_pattern => error || error
//@ reject-flags: --cfg=bad_c_string_upper || --cfg=bad_c_string_upper => inclusive range pattern requires an upper bound || error
//@ reject-flags: --cfg=bad_c_string_lower || --cfg=bad_c_string_lower => no RHS value || error
//@ reject-flags: --cfg=bad_char_attribute || --cfg=bad_char_attribute => error || error
//@ reject-flags: --cfg=bad_float_attribute || --cfg=bad_float_attribute => error || error

macro_rules! emit_pattern {
    ($pattern:pat_param) => {
        fn inspect(value: u8) {
            match value { $pattern => (), _ => () }
        }
    };
}

#[cfg(bad_c_string_pattern)]
emit_pattern!(c"branch");
#[cfg(bad_c_string_upper)]
emit_pattern!(..=c"branch");
#[cfg(bad_c_string_lower)]
emit_pattern!('a'..=c"branch");

macro_rules! bad_attributes {
    (character) => { #[doc = 'λ' extra] fn marker() {} };
    (float) => { #[doc = 1.25 extra] fn marker() {} };
}

#[cfg(bad_char_attribute)]
bad_attributes!(character);
#[cfg(bad_float_attribute)]
bad_attributes!(float);

fn main() {
    assert_eq!(stringify!(0_marker), "0_marker");
}
