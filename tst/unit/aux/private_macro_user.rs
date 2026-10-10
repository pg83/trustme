//@ edition: 2021
use macro_source::shout;

pub fn uses() -> &'static str {
    shout!()
}
