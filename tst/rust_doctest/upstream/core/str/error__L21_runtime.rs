// Extracted from library/core/src/str/error.rs:21
#![allow(unused)]
fn main() {
    fn from_utf8_lossy<F>(mut input: &[u8], mut push: F) where F: FnMut(&str) {
        loop {
            match std::str::from_utf8(input) {
                Ok(valid) => {
                    push(valid);
                    break
                }
                Err(error) => {
                    let (valid, after_valid) = input.split_at(error.valid_up_to());
                    unsafe {
                        push(std::str::from_utf8_unchecked(valid))
                    }
                    push("\u{FFFD}");

                    if let Some(invalid_sequence_length) = error.error_len() {
                        input = &after_valid[invalid_sequence_length..]
                    } else {
                        break
                    }
                }
            }
        }
    }
}
