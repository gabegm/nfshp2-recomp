#!/usr/bin/env python3
"""Summarize native eligibility against intrusive GPU dispatch timings."""
import argparse
from collections import Counter
import json
from pathlib import Path


REASONS = ['geometry', 'colour_format_or_mask', 'depth_format', 'destination_blending',
           'raster_features', 'alpha_failure', 'texture_mode', 'scissor_or_size',
           'surface_overlap_or_wrap', 'vertex_depth_or_q', 'configuration', 'residual']


def analyze(content):
    times=Counter();counts=Counter();classes=Counter();primitives=eligible=bbox=eligible_bbox=0
    frames=set();rows=0;hints={}
    reason_primitives=Counter();uniform_reason_ns=Counter();reason_rows=0;heterogeneous_reason_ns=0
    for number,line in enumerate(content.splitlines(),1):
        if not line.strip():continue
        fields=line.split()
        if len(fields)<6:raise ValueError(f'Invalid dispatch row {number}.')
        frame,index=int(fields[0]),int(fields[1]);pipeline=fields[2]
        ns,x,y=map(int,fields[3:6])
        if min(frame,index,ns,x,y)<0:raise ValueError('Negative dispatch metadata.')
        frames.add(frame);rows+=1;times[pipeline]+=ns;counts[pipeline]+=1
        if pipeline!='raster':continue
        if len(fields)<18 or fields[14]!='native':
            raise ValueError('Raster rows need the PS2_NATIVE_COVERAGE=1 extension.')
        prims,tiles,entries,snapshot,kind,formats,flags,textured=map(int,fields[6:14])
        accepted,box,accepted_box=map(int,fields[15:18])
        if prims<=0 or not 0<=accepted<=prims or not 0<=accepted_box<=box or not 0<=textured<=prims or min(tiles,entries,snapshot,kind,formats,flags)<0:
            raise ValueError('Invalid primitive coverage counts.')
        if (accepted==0 and accepted_box!=0) or (accepted==prims and accepted_box!=box):
            raise ValueError('Inconsistent bounding-box counts.')
        if len(fields)>18:
            if fields[18]!='reasons' or len(fields)<20:raise ValueError('Invalid rejection histogram.')
            histogram={}
            for item in fields[19:]:
                parts=item.split(':')
                if len(parts)!=2:raise ValueError('Invalid rejection entry.')
                mask,count=map(int,parts)
                if not 0<=mask<(1<<len(REASONS)) or count<=0 or mask in histogram:raise ValueError('Invalid rejection mask/count.')
                histogram[mask]=count
            if sum(histogram.values())!=prims or histogram.get(0,0)!=accepted:
                raise ValueError('Rejection histogram does not match primitive totals.')
            reason_rows+=1
            for mask,count in histogram.items():reason_primitives[mask]+=count
            if len(histogram)==1:uniform_reason_ns[next(iter(histogram))]+=ns
            else:heterogeneous_reason_ns+=ns
        category='fully_eligible' if accepted==prims else 'mixed' if accepted else 'unsupported'
        classes[category]+=ns;primitives+=prims;eligible+=accepted;bbox+=box;eligible_bbox+=accepted_box
        key=(category,kind,formats,flags)
        entry=hints.setdefault(key,dict(category=category,kind=kind,first_setup_formats=dict(frame=formats&255,texture=(formats>>8)&255,depth=(formats>>16)&255),first_setup_flags=flags,nanoseconds=0,primitives=0,eligible=0))
        entry['nanoseconds']+=ns;entry['primitives']+=prims;entry['eligible']+=accepted
    raster=times['raster'];total=sum(times.values())
    if not rows or raster<=0 or total<=0:raise ValueError('No timed raster workload found.')
    return dict(version=1,dispatches=rows,frame_labels=len(frames),nanoseconds=dict(times),dispatch_counts=dict(counts),
                raster_classes_ns={k:classes[k] for k in ('fully_eligible','mixed','unsupported')},
                raster_share_of_measured_compute_percent=100*raster/total,
                fully_eligible_raster_percent=100*classes['fully_eligible']/raster,
                possibly_eligible_raster_percent=100*(classes['fully_eligible']+classes['mixed'])/raster,
                fully_eligible_compute_percent=100*classes['fully_eligible']/total,
                possibly_eligible_compute_percent=100*(classes['fully_eligible']+classes['mixed'])/total,
                primitives=primitives,eligible_primitives=eligible,eligible_primitive_percent=100*eligible/primitives,
                bbox_pixels=bbox,eligible_bbox_pixels=eligible_bbox,
                state_hints=sorted(hints.values(),key=lambda v:v['nanoseconds'],reverse=True)[:16],
                rejection_profile=dict(raster_dispatches_with_reasons=reason_rows,
                    combinations=[dict(mask=mask,reasons=[name for bit,name in enumerate(REASONS) if mask&(1<<bit)],
                        primitives=count,uniform_dispatch_nanoseconds=uniform_reason_ns[mask])
                        for mask,count in sorted(reason_primitives.items(),key=lambda item:item[1],reverse=True)],
                    heterogeneous_dispatch_nanoseconds=heterogeneous_reason_ns,
                    single_condition_dispatch_ns={name:uniform_reason_ns[1<<bit] for bit,name in enumerate(REASONS)},
                    interpretation='Only dispatches with one identical mask receive time per combination. Conditions can coexist; masks describe blockers, not predicted savings.'),
                limitations=['Intrusive per-dispatch GPU timings; not ordinary frame time or an FPS forecast.',
                             'Mixed dispatch time cannot be apportioned to eligible primitives from counts or bounding boxes.',
                             'Eligibility uses the existing native guard; it is not a proof of raster equivalence or a speedup.',
                             'Bounding-box areas overlap and are not measured fragment coverage.',
                             'State hints describe the first setup, which may not describe a whole mixed batch.',
                             'Rejection masks describe known conditions; one reported condition does not prove a replacement would be correct or fast.'])


def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('trace',type=Path);parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args();root=Path(__file__).resolve().parents[1]
    if not args.output.resolve().is_relative_to((root/'.local').resolve()):parser.error('Write private profile output under .local.')
    try:result=analyze(args.trace.read_text())
    except (OSError,ValueError) as error:parser.error(str(error))
    args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n')
    print(json.dumps({k:v for k,v in result.items() if k not in ('state_hints','limitations','rejection_profile')},indent=2))

if __name__=='__main__':main()
