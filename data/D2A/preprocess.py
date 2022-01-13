#!/usr/bin/env python3

import sys
import os

curPath = os.path.abspath(os.path.dirname(__file__))
rootPath = os.path.split(curPath)[0]
sys.path.append(rootPath)

import json
import gzip
import pickle
from typing import Dict
from git import Repo, Git
from os.path import join, basename, exists, dirname
from git.objects import commit
from pydriller import Repository, Commit
from typing import List, Dict
import json
import os
import numpy as np
from pydriller.domain.commit import Method, ModifiedFile
from git import Repo, Git


def checkout_to(repo_dir, commit_id):

    print(f'checkout workspace to {commit_id}')
    r = Git(repo_dir)

    try:
        r.execute('git checkout ' + commit_id + ' -f', shell=True)
    except Exception as e:
        # r.execute('git branch -D ' + commit_id, shell=True)
        pass
    print('checkout end !')
    r.update_environment()
    return r


def checkout_back(r, project):
    backname = 'master'
    if project == 'httpd':
        backname = 'trunk'
    elif project == 'libgit2':
        backname = 'main'
    print('checkout workspace to current')
    try:
        r.execute('git checkout ' + backname + ' -f', shell=True)
    except Exception as e:
        pass
    print('checkout end !')


def read_pkl(file_path: str) -> Dict:
    """
    read sample from pkl
    
    Args:
        file_path: path to pkl file

    """
    data = []
    with gzip.open(file_path, mode="rb") as fp:
        while True:
            try:
                item = pickle.load(fp)
                data.append(item)
            except EOFError:
                break
    return data

def write_source_code(file_path: str, commit_root: str, project_root: str):
    dir_name = dirname(file_path)
    if not exists(join(f"{commit_root}/files", dir_name)):
        os.makedirs(join(f"{commit_root}/files", dir_name))
    if not exists(join(f"{commit_root}/files", file_path)):
        tgt = join(f"{commit_root}/files", file_path)
        os.system(f"cp {join(project_root, file_path)} {tgt}")
        fs = []
        if exists(f"{commit_root}/files.json"):

            with open(f"{commit_root}/files.json", "r") as f:
                fs = json.load(f)
        fs.append(file_path)
        with open(f"{commit_root}/files.json", "w") as f:
            json.dump(fs, f, indent=2)

def init_output_dirs(out_root, project, commit_id):
    commit_root = f"{out_root}/{project}/{commit_id}"
    if not exists(f"{out_root}/{project}/"):
        os.mkdir(f"{out_root}/{project}/")
    if not exists(commit_root):
        os.mkdir(commit_root)
        os.mkdir(f"{commit_root}/bcs")
        os.mkdir(f"{commit_root}/files")
        os.mkdir(f"{commit_root}/graphs")

def after_fix_d2a(project: str):
    """
    generate label for d2a dataset after fix 0
    """
    print(f"start -- After Fix 0 for {project}.")
    after_fix_file_path = f"datasets/{project}/{project}_after_fix_extractor_0.pickle.gz"
    if not exists(after_fix_file_path):
        return
    after_fix_label = read_pkl(after_fix_file_path)
    print(f"Done load raw label data: {after_fix_file_path}")

    out_root = "outputs"
    done_idxes_path = f"datasets/{project}/done.txt"
    project_root = join("projects", project)

    # load processed ids
    if not exists(done_idxes_path):
        os.system(f"touch {done_idxes_path}")
    with open(done_idxes_path, "r") as f:
        done_idxes = set(f.read().split(","))
    ct = 0
    for sample in after_fix_label:
        if sample["id"] in done_idxes:
            print(f"skip {sample['id']}")
            ct += 1
            continue

        commit_id = sample["version_pair"]["after"]
        commit_root = f"{out_root}/{project}/{commit_id}"
        print(
            f"Processing: {project}/{commit_id}. Label: {sample['label']}")
        labels = []
        # check to target commit id to copy source files
        repo = checkout_to(project_root, commit_id)
        init_output_dirs(out_root, project, commit_id)
        if exists(f"{out_root}/{project}/{commit_id}/label.json"):
            print(f"Exists: {out_root}/{project}/{commit_id}/label.json")
            with open(f"{out_root}/{project}/{commit_id}/label.json",
                      "r") as f:
                labels = json.load(f)
        d2a_label = {}
        d2a_label["label"] = 0
        d2a_label["bug_type"] = sample["bug_type"]
        d2a_label["bug_line"] = None
        d2a_label["project"] = project
        d2a_label["commitid"] = commit_id
        d2a_label["file_name"] = None
        d2a_label["file_path"] = None
        d2a_label["compiler_args"] = sample["compiler_args"]
        d2a_label["method"] = None
        d2a_label["trace"] = list()
        d2a_label["source"] = "after0"
        for trace in sample["trace"]:

            tr = dict()
            tr["idx"] = trace["idx"]
            tr["level"] = trace["level"]
            file_path = trace["file"]
            tr["file_path"] = file_path
            tr["loc"] = trace["loc"]
            # write each file in the trace
            write_source_code(file_path, commit_root, project_root)
            if trace["func_key"] is None:
                tr["method"] = None
                d2a_label["trace"].append(tr)
                continue

            spts = trace["func_key"].split("@")[1].split("-")
            # method: [name, startline, endline]
            tr["method"] = [
                trace["func_name"],
                int(spts[0].split(":")[0]),
                int(spts[1].split(":")[0])
            ]

            d2a_label["trace"].append(tr)
        # trace lines passing th bug-triggering method
        d2a_label["method_trace"] = None

        safe_funcs = list()
        funcs = sample["functions"]
        for func_key in funcs:
            # all the functions are safe
            func = funcs[func_key]
            file_path = func["file"]
            method_name = func["name"]

            # we only record changed method
            # if func["touched_by_commit"]:
            label = {}
            print(f"find safe method: {file_path}/{method_name}")
            write_source_code(file_path, commit_root, project_root)
            label["file_path"] = file_path
            label["file_name"] = basename(file_path)
            spts = func["loc"].split("-")
            start_line = int(spts[0].split(":")[0])
            end_line = int(spts[1].split(":")[0])

            label["method"] = [method_name, start_line, end_line]
            method_flaws = set()
            for trace in d2a_label["trace"]:
                if trace["loc"] is None:
                    continue
                line_no = int(trace["loc"].split(":")[0])
                if (trace["file_path"] == file_path) and (
                        line_no >= start_line) and (line_no <= end_line):
                    method_flaws.add(line_no)
            # safe method trace
            label["method_trace"] = list(method_flaws)

            safe_funcs.append(label)

        d2a_label["label_funcs"] = safe_funcs
        d2a_label["all_funcs"] = None
        labels.append(d2a_label)
        checkout_back(repo, project)
        ct += 1
        with open(f"{out_root}/{project}/{commit_id}/label.json",
                  "w",
                  encoding="utf-8") as f:
            json.dump(labels, f, indent=2)
        print(f"Done writing: {commit_root}/label.json")
        with open(done_idxes_path, "a") as f:
            f.write(sample["id"] + ",")
    print(
        f"end -- Auto Labeler 0 for {project}. passed {ct}/{len(after_fix_label)}"
    )

def auto_label1_d2a(project: str):
    print(f"start -- Auto Labeler 1 for {project}.")
    labeler_1_file_path = f"datasets/{project}/{project}_labeler_1.pickle.gz"
    if not exists(labeler_1_file_path):
        return
    raw_label_1 = read_pkl(labeler_1_file_path)
    print(f"Done load raw label data: {labeler_1_file_path}")

    out_root = "outputs"
    done_idxes_path = f"datasets/{project}/done.txt"
    project_root = join("projects", project)

    # load processed ids
    if not exists(done_idxes_path):
        os.system(f"touch {done_idxes_path}")
    with open(done_idxes_path, "r") as f:
        done_idxes = set(f.read().split(","))
    ct = 0
    for sample in raw_label_1:
        if sample["id"] in done_idxes:
            print(f"skip {sample['id']}")
            ct += 1
            continue
        labels = []
        commit_id = sample["versions"]["before"]
        print(
            f"Processing: {project}/{commit_id}. Label: {sample['label']}")
        commit_root = f"{out_root}/{project}/{commit_id}"

        # check to target commit id to copy source files
        repo = checkout_to(project_root, commit_id)
        init_output_dirs(out_root, project, commit_id)

        if exists(f"{out_root}/{project}/{commit_id}/label.json"):
            print(f"Exists: {out_root}/{project}/{commit_id}/label.json")
            with open(f"{out_root}/{project}/{commit_id}/label.json",
                      "r") as f:
                labels = json.load(f)

        bug_info = sample["bug_info"] if sample[
            "adjusted_bug_loc"] is None else sample["adjusted_bug_loc"]
        bug_line = bug_info["line"]
        bug_file = bug_info["file"]
        file_name = basename(bug_file)
        # write bug file
        write_source_code(bug_file, commit_root, project_root)

        d2a_label = {}
        d2a_label["label"] = 1
        d2a_label["bug_type"] = sample["bug_type"]
        d2a_label["bug_line"] = bug_line
        d2a_label["project"] = project
        d2a_label["commitid"] = commit_id
        d2a_label["file_name"] = file_name
        d2a_label["file_path"] = bug_file
        d2a_label["compiler_args"] = sample["compiler_args"]
        d2a_label["trace"] = list()
        d2a_label["source"] = "auto1"
        method_flaws = set()
        for trace in sample["trace"]:

            tr = dict()
            tr["idx"] = trace["idx"]
            tr["level"] = trace["level"]
            file_path = trace["file"]
            tr["file_path"] = file_path
            tr["loc"] = trace["loc"]
            # write each file in the trace
            write_source_code(file_path, commit_root, project_root)
            if trace["func_key"] is None:
                tr["method"] = None
                d2a_label["trace"].append(tr)
                continue

            spts = trace["func_key"].split("@")[1].split("-")
            # method: [name, startline, endline]
            tr["method"] = [
                trace["func_name"],
                int(spts[0].split(":")[0]),
                int(spts[1].split(":")[0])
            ]
            if (int(trace["loc"].split(":")[0]) == bug_info["line"]
                    and trace["file"] == bug_info["file"]):
                d2a_label["method"] = tr["method"]

            d2a_label["trace"].append(tr)

        if "method" in d2a_label:
            # add the lines in the trace passing the bug-triggering method
            for trace in d2a_label["trace"]:
                if trace["loc"] is None:
                    continue
                line_no = int(trace["loc"].split(":")[0])
                if (trace["file_path"] == bug_file) and (
                        line_no >= d2a_label["method"][1]) and (
                            line_no <= d2a_label["method"][2]):
                    method_flaws.add(line_no)
            # trace lines passing th bug-triggering method
            d2a_label["method_trace"] = list(method_flaws)
        else:
            d2a_label["method"] = None
            d2a_label["method_trace"] = None

        funcs = sample["functions"]
        all_funcs = list()
        vul_func_labels = list()
        for func_key in funcs:
            file_path = funcs[func_key]["file"]
            method_name = funcs[func_key]["name"]
            spts = funcs[func_key]["loc"].split("-")
            start_line, end_line = int(spts[0].split(":")[0]), int(
                spts[1].split(":")[0])
            all_funcs.append([file_path, method_name, start_line, end_line])
            write_source_code(file_path, commit_root, project_root)

            # we treat modified method as vulnerable methods
            if (funcs[func_key]["touched_by_commit"]):

                print(
                    f"find vulnerable method: {file_path}/{method_name}")
                file_name = basename(file_path)
                label = {}
                label["file_path"] = file_path
                label["file_name"] = file_name
                label["method"] = [method_name, start_line, end_line]

                method_flaws = set()
                for trace in d2a_label["trace"]:
                    if trace["loc"] is None:
                        continue
                    line_no = int(trace["loc"].split(":")[0])
                    if (trace["file_path"] == file_path) and (
                            line_no >= start_line) and (line_no <= end_line):
                        method_flaws.add(line_no)
                # vulnerable method trace
                label["method_trace"] = list(method_flaws)

                vul_func_labels.append(label)
        # all functions recorded in D2A
        d2a_label["all_funcs"] = all_funcs

        d2a_label["label_funcs"] = vul_func_labels
        print(
            f"Done record bug-triggering method: {bug_file}:{bug_line}")
        ct += 1
        checkout_back(repo, project)
        labels.append(d2a_label)

        with open(f"{commit_root}/label.json", "w", encoding="utf-8") as f:
            json.dump(labels, f, indent=2)
        print(f"Done writing: {commit_root}/label.json")
        with open(done_idxes_path, "a") as f:
            f.write(sample["id"] + ",")
    print(
        f"end -- Auto Labeler 1 for {project}. passed {ct}/{len(raw_label_1)}")

def auto_label0_d2a(project: str):
    print(f"start -- Auto Labeler 0 for {project}.")
    labeler_0_file_path = f"datasets/{project}/{project}_labeler_0.pickle.gz"
    if not exists(labeler_0_file_path):
        return
    raw_label_0 = read_pkl(labeler_0_file_path)
    print(f"Done load raw label data: {labeler_0_file_path}")

    out_root = "outputs"
    done_idxes_path = f"datasets/{project}/done.txt"
    project_root = join("projects", project)
    # load processed ids
    if not exists(done_idxes_path):
        os.system(f"touch {done_idxes_path}")
    with open(done_idxes_path, "r") as f:
        done_idxes = set(f.read().split(","))
    ct = 0
    for sample in raw_label_0:
        if sample["id"] in done_idxes:
            print(f"skip {sample['id']}")
            ct += 1
            continue
        labels = []
        commit_id = sample["versions"]["before"]
        commit_root = f"{out_root}/{project}/{commit_id}"
        print(
            f"Processing: {project}/{commit_id}. Label: {sample['label']}")

        # check to target commit id to copy source files
        repo = checkout_to(project_root, commit_id)
        init_output_dirs(out_root, project, commit_id)
        if exists(f"{out_root}/{project}/{commit_id}/label.json"):
            print(f"Exists: {out_root}/{project}/{commit_id}/label.json")
            with open(f"{out_root}/{project}/{commit_id}/label.json",
                      "r") as f:
                labels = json.load(f)

        bug_info = sample["bug_info"] if sample[
            "adjusted_bug_loc"] is None else sample["adjusted_bug_loc"]
        bug_line = bug_info["line"]
        bug_file = bug_info["file"]
        file_name = basename(bug_file)
        write_source_code(bug_file, commit_root, project_root)

        d2a_label = {}
        d2a_label["label"] = 0
        d2a_label["bug_type"] = sample["bug_type"]
        d2a_label["bug_line"] = bug_line
        d2a_label["project"] = project
        d2a_label["commitid"] = commit_id
        d2a_label["file_name"] = file_name
        d2a_label["file_path"] = bug_file
        d2a_label["compiler_args"] = sample["compiler_args"]
        d2a_label["trace"] = list()
        d2a_label["source"] = "auto0"
        method_flaws = set()
        for trace in sample["trace"]:

            tr = dict()
            tr["idx"] = trace["idx"]
            tr["level"] = trace["level"]
            file_path = trace["file"]
            tr["file_path"] = file_path
            tr["loc"] = trace["loc"]
            # write each file in the trace
            write_source_code(file_path, commit_root, project_root)
            if trace["func_key"] is None:
                tr["method"] = None
                d2a_label["trace"].append(tr)
                continue

            spts = trace["func_key"].split("@")[1].split("-")
            # method: [name, startline, endline]
            tr["method"] = [
                trace["func_name"],
                int(spts[0].split(":")[0]),
                int(spts[1].split(":")[0])
            ]
            if (int(trace["loc"].split(":")[0]) == bug_info["line"]
                    and trace["file"] == bug_info["file"]):
                d2a_label["method"] = tr["method"]

            d2a_label["trace"].append(tr)

        if "method" in d2a_label:
            # add the lines in the trace passing the bug-triggering method
            for trace in d2a_label["trace"]:
                if trace["loc"] is None:
                    continue
                line_no = int(trace["loc"].split(":")[0])
                if (trace["file_path"] == bug_file) and (
                        line_no >= d2a_label["method"][1]) and (
                            line_no <= d2a_label["method"][2]):
                    method_flaws.add(line_no)
            # trace lines passing th bug-triggering method
            d2a_label["method_trace"] = list(method_flaws)
        else:
            d2a_label["method"] = None
            d2a_label["method_trace"] = None

        funcs = sample["functions"]
        safe_func_labels = list()
        for func_key in funcs:
            # all the methods in the trace are safe
            file_path = funcs[func_key]["file"]
            method_name = funcs[func_key]["name"]
            print(f"find safe method: {file_path}/{method_name}")
            spts = funcs[func_key]["loc"].split("-")
            start_line, end_line = int(spts[0].split(":")[0]), int(
                spts[1].split(":")[0])
            write_source_code(file_path, commit_root, project_root)

            file_name = basename(file_path)
            label = {}
            label["file_path"] = file_path
            label["file_name"] = basename(file_path)
            label["method"] = [method_name, start_line, end_line]

            method_flaws = set()
            for trace in d2a_label["trace"]:
                if trace["loc"] is None:
                    continue
                line_no = int(trace["loc"].split(":")[0])
                if (trace["file_path"] == file_path) and (
                        line_no >= start_line) and (line_no <= end_line):
                    method_flaws.add(line_no)
            # safe method trace
            label["method_trace"] = list(method_flaws)

            safe_func_labels.append(label)

        d2a_label["label_funcs"] = safe_func_labels
        d2a_label["all_funcs"] = None
        print(
            f"Done record bug-triggering method: {bug_file}:{bug_line}")

        checkout_back(repo, project)
        labels.append(d2a_label)

        with open(f"{out_root}/{project}/{commit_id}/label.json",
                  "w",
                  encoding="utf-8") as f:
            json.dump(labels, f, indent=2)
        print(f"Done writing: {commit_root}/label.json")
        with open(done_idxes_path, "a") as f:
            f.write(sample["id"] + ",")
    print(
        f"end -- Auto Labeler 0 for {project}. passed {ct}/{len(raw_label_0)}")


if __name__ == "__main__":
    project = sys.argv[1]
    auto_label1_d2a(project)
    auto_label0_d2a(project)
    after_fix_d2a(project)