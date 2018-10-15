# VkColors

This program generates colorful images using algorithms created by [Jozsef Fejes](http://joco.name/2014/03/02/all-rgb-colors-in-one-image/).

![](https://i.imgur.com/jwsbkkK.png)

## Usage

VkColors can be invoked from the command line

```console
VkColors [options]
```

There are multiple options to change the behavior of the image generator.

- `--shader=[shader]`

  This selects the algorithm to use in the image generator. Values that can be used are `wave` and `coral`. Defautl is `coral`.

- `--size=[width]x[height]`

  This sets the size of the image. Valid values are any size between `1x1` and `4096x4096`. Default is `512x512`.

- `--source=[source]`

  This sets the method used to color the image. Values that can be used are `shuffle` and `hue`. Default is `shuffle`.

- `--seed=[seed]`

  This sets the seed used by the random number generator. Must be a 32-bit unsigned value. Default is based on system time.

Other options can be set, but this may result in strange behavior or crashing.

- `--workgroupsize=[size]`

  This sets the size of the compute work group used. Must be positive. Default is 64 on AMD hardware and 32 on anything else.

- `--maxbatchabsolute=[size]`

  This sets the maximum number of pixels that can be generated in one batch. Must be positive. Default is 64.

- `--maxbatchrelative=[size]`

  This sets the maximum number of pixels that can be generated, based on the current state of the image. Must be positive. Default is 1024.

## Build

This project uses CMake as its build system.
