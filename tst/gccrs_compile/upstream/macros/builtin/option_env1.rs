
use std::option::Option;


fn main() {
    // Both a guaranteed-to-exist variable and a failed find should compile
    let _: Option<&str> = option_env!("PWD");
    let _: Option<&str> = option_env!("PROBABLY_DOESNT_EXIST");
}
