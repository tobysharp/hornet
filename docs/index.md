# Hornet Node

## Motivation

> ## *"Bitcoin is an extraordinarily elegant protocol. I wanted to use my 30 years' of experience writing realtime production software to express it in the most elegant, modern, efficient code possible."*

Hornet Node is a consensus-compatible Bitcoin client designed from the ground up to be elegant, modular, rigorous, efficient, and modern. It is not copied or forked from any prior starting point, and it has no external dependencies. It stands alone.

## Goals




## Status

Hornet Node is in active development and not yet ready for general use. See the Milestones below for demos and future releases.


## Design

- Conciseness (e.g. no sprawling functions)
- Separation of concerns (e.g. metadata sidecars)
- Efficient data structures (e.g. for timechain and UTXOs)
- Contiguous memory layout (e.g. no jagged arrays)
- Modular layer cake (e.g. protocol / consensus separate from data / networking)
- Strictly one-way dependencies between modules
- Use of modern C++ for clarity (e.g. lambda functions, variadic templates, ...)

## Contributions

Here are a few examples of how the design principles translate into features.

### Consensus domain-specific language (DSL)

### Timechain chain-tree data structure

### Metadata sidecars

### Flat arrays

### Polymorphic message dispatch

### Interactive web UI

### UTXO cache


## Examples

[Examples](examples.md)

## Author

Hi, I'm Toby Sharp and I'm the author of Hornet Node.

I'm a mathematician with 30 years' [experience](www.linkedin.com/in/tobylsharp) developing commercial realtime software systems in C++ at the intersection of research and products. Most recently, I was Principal Software Scientist at Microsoft (2005-2022) and now Lead Software Architect at Google (2022-) for Android XR. 

I've [published](https://scholar.google.com/citations?user=OOcllDwAAAAJ) 20+ papers at leading computer vision conferences and journals on novel high-performance algorithms, and had the honor of receiving a few significant [awards](https://www.linkedin.com/in/tobylsharp/details/honors/) for my contributions, especially for low-level performance in novel machine learning algorithms.

I've also had the privilege of designing and coding several major computer vision components in realtime large-scale products including [Microsoft Kinect Body Tracking SDK](https://azure.microsoft.com/en-us/products/kinect-dk#content-card-list-oc3409), [Microsoft Kinect Fusion](https://www.microsoft.com/en-us/research/wp-content/uploads/2016/11/ismar_2011.pdf) SDK, [Microsoft HoloLens 2 Hand Tracking](https://learn.microsoft.com/en-us/windows/mixed-reality/discover/mixed-reality), and Google's forthcoming [Android XR](https://www.android.com/xr/) products. During the past few years I have developed state-of-the-art [model fitting](https://arxiv.org/pdf/2204.02776) libraries for mapping 60Hz camera images of hands and faces to parametric kinematic deformable models.

In my industry I have a reputation for clean, robust, and highly optimized code.

For Hornet Node I wanted to apply everything I've learned and teach about software design and apply that to the world's most important software technology — Bitcoin.

## FAQs


### Who is funding Hornet development?

Nobody is funding Hornet Node. I have been developing it in my spare time as a personal passion project, drawing inspiration from Satoshi Nakomoto who gave an incredibly valuable technology to the world and took nothing in return.

### What problem(s) does Hornet Node solve?

### How will you guarantee consensus correctness?

### How will you drive adoption?

I'm focused on contribution rather than adoption, although in my experience good quality software tends to gain market share over time. If there are specific issues blocking adoption I will try to address them in a timely manner.

### What's Hornet Node's policy on spam and filters?

There will be user-configurable settings for your choice of node policy.

### How does Hornet Node compare to Bitcoin Core / Knots?

### Where is the source code?

So far the source code has not yet been made public. My O-1 US visa only covers my work for my employer and not additional work whether paid or unpaid. Therefore I will need a change to my visa terms before I can publish source code.

### Why should anyone run Hornet Node?

## Contact

toby@hornetnode.com