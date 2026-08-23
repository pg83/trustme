#![feature(lang_items, no_core)]
#![no_core]

extern crate artifact_graph;

pub fn consume() {
    artifact_graph::graph_artifact();
}
