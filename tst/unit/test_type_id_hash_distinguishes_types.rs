// `core::any::TypeId` hashes only the second pointer-sized chunk of its 128-bit
// value, so every type has to carry identity in that half as well.
use std::any::TypeId;
use std::collections::HashSet;
use std::hash::{DefaultHasher, Hash, Hasher};

struct A;
struct B;

fn hash_of(id: TypeId) -> u64 {
    let mut hasher = DefaultHasher::new();
    id.hash(&mut hasher);
    hasher.finish()
}

fn main() {
    let ids = [
        TypeId::of::<u8>(),
        TypeId::of::<u32>(),
        TypeId::of::<A>(),
        TypeId::of::<B>(),
        TypeId::of::<Vec<u8>>(),
        TypeId::of::<fn(u8) -> u8>(),
    ];
    let hashes: HashSet<u64> = ids.iter().copied().map(hash_of).collect();
    assert_eq!(hashes.len(), ids.len(), "TypeId hashes collide");
}
