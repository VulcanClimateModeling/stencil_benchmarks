#!/usr/bin/env python

import gc
import itertools

import click
import numpy as np
import pandas as pd

from stencil_benchmarks.benchmark import ExecutionError, ParameterError
from stencil_benchmarks.benchmarks_collection.stencils.cuda_hip import (
    basic, horizontal_diffusion, vertical_advection)
from stencil_benchmarks.tools import cli


def benchmark_domains():
    for exponent in range(5, 11):
        d = 2**exponent
        yield d, d, 80


def truncate_block_size_to_domain(kwargs):
    if 'block_size' in kwargs and 'domain' in kwargs:
        kwargs['block_size'] = tuple(
            min(b, d) for b, d in zip(kwargs['block_size'], kwargs['domain']))
    return kwargs


def benchmark(stencils_and_kwargs, executions, preprocess_kwargs=None):
    if preprocess_kwargs is None:
        preprocess_kwargs = lambda x: x
    results = []
    with cli.ProgressBar() as progress:
        for domain in progress.report(benchmark_domains()):
            for name, Stencil, kwargs in progress.report(stencils_and_kwargs):
                stencil = Stencil(domain=domain, **preprocess_kwargs(kwargs))

                for _ in progress.report(range(executions)):
                    result = stencil.run()
                    result.update(stencil=name)
                    result.update(cli.pretty_parameters(stencil))
                    results.append(result)

                del stencil
                gc.collect()
    return pd.DataFrame(results)


@click.group()
def main():
    pass


def common_kwargs(backend, gpu_architecture, dtype):
    return dict(backend=backend,
                compiler='nvcc' if backend == 'cuda' else 'hipcc',
                gpu_architecture=gpu_architecture,
                verify=False,
                run_twice=True,
                gpu_timers=True,
                alignment=128 if backend == 'cuda' else 64,
                dtype=dtype)


@main.command()
@click.argument('backend', type=click.Choice(['cuda', 'hip']))
@click.argument('gpu-architecture')
@click.argument('output', type=click.Path())
@click.option('--executions', '-e', type=int, default=101)
@click.option('--dtype', '-d', default='float64')
def basic_bandwidth(backend, gpu_architecture, output, executions, dtype):
    kwargs = common_kwargs(backend, gpu_architecture, dtype)
    kwargs.update(
        loop='3D',
        block_size=(32, 8, 1),
        halo=1,
    )

    stream_kwargs = kwargs.copy()
    stream_kwargs.update(loop='1D', block_size=(1024, 1, 1), halo=0)

    stencils = [('stream', basic.Copy, stream_kwargs),
                ('empty', basic.Empty, kwargs), ('copy', basic.Copy, kwargs),
                ('avg-i', basic.OnesidedAverage, dict(axis=0, **kwargs)),
                ('avg-j', basic.OnesidedAverage, dict(axis=1, **kwargs)),
                ('avg-k', basic.OnesidedAverage, dict(axis=2, **kwargs)),
                ('sym-avg-i', basic.SymmetricAverage, dict(axis=0, **kwargs)),
                ('sym-avg-j', basic.SymmetricAverage, dict(axis=1, **kwargs)),
                ('sym-avg-k', basic.SymmetricAverage, dict(axis=2, **kwargs)),
                ('lap-ij', basic.Laplacian,
                 dict(along_x=True, along_y=True, along_z=False, **kwargs))]

    table = benchmark(stencils, executions)
    table.to_csv(output)


@main.command()
@click.argument('backend', type=click.Choice(['cuda', 'hip']))
@click.argument('gpu-architecture')
@click.argument('output', type=click.Path())
@click.option('--executions', '-e', type=int, default=101)
@click.option('--dtype', '-d', default='float32')
def horizontal_diffusion_bandwidth(backend, gpu_architecture, output,
                                   executions, dtype):
    kwargs = common_kwargs(backend, gpu_architecture, dtype)

    otf_kwargs = kwargs.copy()
    classic_kwargs = kwargs.copy()
    jscan_kwargs = kwargs.copy()

    otf_kwargs['loop'] = '3D'
    if backend == 'hip':
        otf_kwargs['block_size'] = (128, 4, 1)
        classic_kwargs['block_size'] = (64, 8, 1)
        jscan_kwargs['block_size'] = (256, 4, 1)
    else:
        otf_kwargs['block_size'] = (256, 2, 1)
        classic_kwargs['block_size'] = (32, 16, 1)
        jscan_kwargs['block_size'] = (256, 2, 2)

    stencils = [('on-the-fly', horizontal_diffusion.OnTheFly, otf_kwargs),
                ('classic', horizontal_diffusion.Classic, classic_kwargs),
                ('j-scan-otf-aligned', horizontal_diffusion.JScanOtfAligned,
                 jscan_kwargs)]

    table = benchmark(stencils,
                      executions,
                      preprocess_kwargs=truncate_block_size_to_domain)
    table.to_csv(output)


@main.command()
@click.argument('backend', type=click.Choice(['cuda', 'hip']))
@click.argument('gpu-architecture')
@click.argument('output', type=click.Path())
@click.option('--executions', '-e', type=int, default=101)
@click.option('--dtype', '-d', default='float32')
def vertical_advection_bandwidth(backend, gpu_architecture, output, executions,
                                 dtype):
    kwargs = common_kwargs(backend, gpu_architecture, dtype)

    kwargs['block_size'] = (1024, 1)

    stencils = [('on-the-fly', vertical_advection.KInnermost, kwargs)]

    table = benchmark(stencils,
                      executions,
                      preprocess_kwargs=truncate_block_size_to_domain)
    table.to_csv(output)


if __name__ == '__main__':
    main()