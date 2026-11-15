//@ edition: 2021
//@ compile-fail: Couldn't find type name 'MissingCov99W1162'

struct Holder<T>(T);
type Invalid = Holder<MissingCov99W1162>;

fn main() {}
