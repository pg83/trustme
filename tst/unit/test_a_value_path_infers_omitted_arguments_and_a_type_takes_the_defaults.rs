// Upstream lowers a path segment without generic arguments with
// `infer_args` when the path is a value, pattern or struct-expression path
// (`ParamMode::Optional`): every parameter is an inference variable and the
// defaults play no part. A type written in a body (`let h: Csr`) is a type
// position, where omitted arguments take their defaults. petgraph's
// `Csr::new()` then `add_edge(.., 5)` must infer `E = i32`, not `()`.
struct Csr<N = (), E = ()> {
    nodes: Vec<N>,
    edges: Vec<E>,
}
impl<N, E> Csr<N, E> {
    fn new() -> Self {
        Csr { nodes: Vec::new(), edges: Vec::new() }
    }
    fn add_edge(&mut self, weight: E) {
        self.edges.push(weight);
    }
}
fn main() {
    let mut g = Csr::new();
    g.add_edge(5);
    g.nodes.push('n');
    assert_eq!(g.edges[0] + 1, 6);
    let h: Csr = Csr::new();
    assert!(h.edges.is_empty() && h.nodes.is_empty());
}
