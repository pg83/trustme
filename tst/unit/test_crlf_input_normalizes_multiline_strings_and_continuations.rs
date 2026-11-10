fn main() {
    let ordinary = "alpha
beta";
    let raw = r"gamma
delta";
    let joined = "left\
        right";
    assert_eq!(ordinary.as_bytes(), b"alpha\nbeta");
    assert_eq!(raw.as_bytes(), b"gamma\ndelta");
    assert_eq!(joined, "leftright");
}
