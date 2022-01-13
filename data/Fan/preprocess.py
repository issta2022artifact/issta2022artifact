#!/usr/bin/env python3
import sys
import os

curPath = os.path.abspath(os.path.dirname(__file__))
rootPath = os.path.split(curPath)[0]
sys.path.append(rootPath)

from git import Repo, Git
from os.path import join, basename, exists, dirname
from pydriller import Repository, Commit
from typing import List, Dict
import json
import os
import numpy as np
from pydriller.domain.commit import Method



def init_output_dirs(out_root, project, commit_id):
    commit_root = f"{out_root}/{project}/{commit_id}"
    if not exists(f"{out_root}/{project}/"):
        os.mkdir(f"{out_root}/{project}/")
    if not exists(commit_root):
        os.mkdir(commit_root)
        os.mkdir(f"{commit_root}/bcs")
        os.mkdir(f"{commit_root}/files")
        os.mkdir(f"{commit_root}/graphs")


def checkout_to(repo_dir, commit_id, project):
    """
    checks to commit_id of repo_dir
    """
    print('checkout workspace to {} from {}'.format(commit_id, project))
    repo = Repo(repo_dir)
    r = Git(repo_dir)
    repo.create_head(project, commit_id)
    try:
        r.execute('git checkout ' + project + ' -f', shell=True)
    except Exception as e:
        print(e)
        r.execute('git branch -D ' + project, shell=True)
        r.execute('git checkout ' + project + ' -f', shell=True)
    print('checkout end !')

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
        print(e)
        r.execute('git checkout unstable -f', shell=True)
    r.execute('git branch -D ' + project, shell=True)
    print('checkout end !')


def checkout_to_pre(r):
    print('checkout workspace to pre')
    r.execute('git checkout HEAD^ -f', shell=True)
    print('checkout end !')


def build_before_to_after(commits: List[Commit]):

    before_to_after = dict()
    for commit in commits:
        for p in commit.parents:
            if p not in before_to_after:
                before_to_after[p] = []
            before_to_after[p].append(commit.hash)
    return before_to_after


def is_cpp(filename):
    """
    Unix:  .C  .cc  .cxx  .c
    GNU:  C++:  .C  .cc  .cxx  .cpp  .c++
    Digital Mars:  .cpp  .cxx
    Borland:  .C++  .cpp
    Watcom:   .cpp
    Microsoft Visual C++:  .cpp  .cxx  .cc
    Metrowerks CodeWarrior:   .cpp  .cp  .cc  .cxx  .c++
    """
    return filename.split(".")[-1] in [
        "c", "cpp", "cc", "cxx", "C", "c++", "C++"
        "cp", "Cpp"
    ]


def build_name_to_method(methods):
    '''
    :param methods_before:
    :return:
    '''
    res = dict()
    stline_to_method = dict()
    for method in methods:
        res[method.long_name] = method
        stline_to_method[method.start_line] = method.long_name
    return res, stline_to_method


def find_vul_methods(deleted_lines: List, stline_to_method_before: Dict):

    res = []
    stlines = sorted(stline_to_method_before.keys())
    idxs = set(np.searchsorted(stlines, deleted_lines, side="right"))
    for idx in idxs:
        res.append(stline_to_method_before[stlines[idx - 1]])
    return res


def match_method_to_codelines(method: Method, code_firstline: str,
                              code_lines: str) -> bool:
    if method.name not in code_firstline:
        return False
    for para in method.parameters:
        if para not in code_lines:
            return False
    if code_firstline[code_firstline.index(method.name) -
                      1] not in [" ", "&", "*"]:
        return False
    st_idx = code_firstline.index(method.name) + len(method.name)
    ed_idx = code_firstline.index("(")
    return code_firstline[st_idx:ed_idx].strip() == ""


def write_source_code(file_path, commit_root, source_code):
    dir_name = dirname(file_path)
    if not exists(join(f"{commit_root}/files", dir_name)):
        os.makedirs(join(f"{commit_root}/files", dir_name))
    if not exists(join(f"{commit_root}/files", file_path)):
        with open(join(f"{commit_root}/files", file_path),
                  "w",
                  encoding="utf-8") as f:
            f.write(source_code)
        fs = []
        if exists(f"{commit_root}/files.json"):

            with open(f"{commit_root}/files.json", "r") as f:
                fs = json.load(f)
        fs.append(file_path)
        with open(f"{commit_root}/files.json", "w") as f:
            json.dump(fs, f, indent=2)


def auto_label_devign():
    """
    generate label for devign dataset
    TODO: generate bc
    """
    print("start -- label for devign.")
    out_root = "outputs"
    # project = "FFmpeg"
    raw_json = "function.json"
    done_idxes_path = "done.txt"
    # load processed ids
    if not exists(done_idxes_path):
        os.system(f"touch {done_idxes_path}")
    with open(done_idxes_path, "r") as f:
        done_idxes = set(f.read().split(","))
    # before_to_after = build_before_to_after(
    #     list(Repository(join(src_root, project)).traverse_commits()))

    with open(raw_json, "r", encoding="utf-8", errors="ignore") as f:
        raw_label = json.load(f)
    print(f"Done load raw label data: {raw_json}")
    ct = 0
    for idx, sample in enumerate(raw_label):
        if str(idx) in done_idxes:
            print(
                f"skip {idx}: {sample['project']}/{sample['commit_id']}")
            continue
        labels = []
        # if sample["project"] != project:
        #     continue
        project = sample["project"]
        project_root = join("projects", project)
        code = sample["func"]
        code_firstline = code.split("\n\n")[0]
        code_lines = " ".join(code.split("\n\n")[0:20])
        try:
            if sample["target"] == 0:
                commit_id = sample["commit_id"]
                commit_root = f"{out_root}/{project}/{commit_id}"
                print(
                    f"Processing: {project}/{commit_id}. Label: {sample['target']}"
                )
                init_output_dirs(out_root, project, commit_id)
                if exists(f"{out_root}/{project}/{commit_id}/label.json"):
                    print(
                        f"Exists: {out_root}/{project}/{commit_id}/label.json")
                    with open(f"{out_root}/{project}/{commit_id}/label.json",
                              "r") as f:
                        labels = json.load(f)

                repo = Repository(project_root, single=commit_id)
                commit = list(repo.traverse_commits())[0]

                for modi_file in commit.modified_files:
                    if (len(modi_file.methods_before) == 0):
                        continue
                    if (len(modi_file.methods) == 0):
                        continue

                    for method in modi_file.methods:
                        if match_method_to_codelines(method, code_firstline,
                                                     code_lines):
                            safe_method = method
                            file_path = modi_file.new_path
                            file_name = basename(file_path)

                            print(
                                f"find method: {file_path}/{safe_method.long_name}"
                            )
                            write_source_code(file_path, commit_root,
                                              modi_file.source_code)

                            label = {}
                            label["label"] = 0
                            label["project"] = project
                            label["commitid"] = commit_id
                            label["file_name"] = file_name
                            label["file_path"] = file_path

                            label["method"] = [
                                safe_method.name, safe_method.long_name,
                                safe_method.start_line, safe_method.end_line
                            ]
                            label["file_flaws"] = []
                            label["method_flaws"] = []
                            label["flows"] = [[]]
                            label["source"] = "devign"
                            labels.append(label)
                if len(labels) == 0:
                    os.system(f"rm -r {out_root}/{project}/{commit_id}")
                    with open(done_idxes_path, "a") as f:
                        f.write(str(idx) + ",")
                    continue
                ct += 1
                with open(f"{out_root}/{project}/{commit_id}/label.json",
                          "w",
                          encoding="utf-8") as f:
                    json.dump(labels, f, indent=2)
                print(f"Done writing: {commit_root}/label.json")
            else:
                # ct = ct - 1
                commit_id = sample["commit_id"]
                # after_commits = before_to_after[commit_id]
                repo = Repository(project_root, single=commit_id)
                commit = list(repo.traverse_commits())[0]
                before_commit_id = commit.parents[0]
                commit_root = f"{out_root}/{project}/{before_commit_id}"
                print(
                    f"Processing: {project}/{before_commit_id}. Label: {sample['target']}"
                )
                init_output_dirs(out_root, project, before_commit_id)
                if exists(
                        f"{out_root}/{project}/{before_commit_id}/label.json"):
                    print(
                        f"Exists: {out_root}/{project}/{before_commit_id}/label.json"
                    )
                    with open(
                            f"{out_root}/{project}/{before_commit_id}/label.json",
                            "r") as f:
                        labels = json.load(f)

                for modi_file in commit.modified_files:
                    if (len(modi_file.methods_before) == 0):
                        continue
                    name_to_method_before, stline_to_method_before = build_name_to_method(
                        modi_file.methods_before)

                    file_path = modi_file.old_path
                    if file_path is not None:
                        file_name = basename(file_path)

                        # contain deleted lines
                        if is_cpp(file_name) and modi_file.deleted_lines > 0:
                            deleted_lines = [
                                i[0] for i in modi_file.diff_parsed["deleted"]
                            ]
                            vul_methods = find_vul_methods(
                                deleted_lines, stline_to_method_before)
                            for vul_method in vul_methods:
                                if match_method_to_codelines(
                                        name_to_method_before[vul_method],
                                        code_firstline, code_lines):
                                    v_method = name_to_method_before[
                                        vul_method]
                                    print(
                                        f"find method: {file_path}/{v_method.long_name}"
                                    )
                                    write_source_code(
                                        file_path, commit_root,
                                        modi_file.source_code_before)
                                    label = {}
                                    label["label"] = 1
                                    label["project"] = project
                                    label["commitid"] = before_commit_id
                                    label["file_name"] = file_name
                                    label["file_path"] = file_path

                                    label["method"] = [
                                        v_method.name, v_method.long_name,
                                        v_method.start_line, v_method.end_line
                                    ]
                                    label["file_flaws"] = deleted_lines
                                    label["source"] = "devign"

                                    label["method_flaws"] = [
                                        deleted_line
                                        for deleted_line in deleted_lines if ((
                                            deleted_line >= v_method.start_line
                                        ) and (
                                            deleted_line <= v_method.end_line))
                                    ]

                                    label["flows"] = [[]]
                                    labels.append(label)
                if len(labels) == 0:
                    os.system(f"rm -r {out_root}/{project}/{before_commit_id}")
                    with open(done_idxes_path, "a") as f:
                        f.write(str(idx) + ",")
                    continue
                ct += 1
                with open(
                        f"{out_root}/{project}/{before_commit_id}/label.json",
                        "w",
                        encoding="utf-8") as f:
                    json.dump(labels, f, indent=2)
                print(f"Done writing: {commit_root}/label.json")
        except Exception as e:
            pass

        with open(done_idxes_path, "a") as f:
            f.write(str(idx) + ",")
    print(f"end -- label for devign. passed {ct}/{len(raw_label)}")


def auto_label_fan():
    """
    generate label for fan (https://github.com/ZeoVan/MSR_20_Code_vulnerability_CSV_Dataset) dataset
    """

    print("start -- label for Fan.")
    src_root = "projects"
    out_root = "outputs"
    raw_json = "MSR_data_simple.json"
    done_idxes_path = "done.txt"
    if not exists(done_idxes_path):
        os.system(f"touch {done_idxes_path}")
    with open(done_idxes_path, "r") as f:
        done_idxes = set(f.read().split(","))
    # before_to_after = build_before_to_after(
    #     list(Repository(join(src_root, project)).traverse_commits()))
    with open(raw_json, "r", encoding="utf-8", errors="ignore") as f:
        raw_label = json.load(f)
    ct = 0
    for idx in raw_label:
        sample = raw_label[idx]
        if str(idx) in done_idxes:
            print(
                f"skip {idx}: {sample['project']}/{sample['commit_id']}")
            continue
        labels = []
        project = sample["project"]
        if not exists(f"projects/{project}"):
            continue
        try:
            if int(sample["label"]) == 0:

                code = sample["func_after"]
                code_firstline = code.split("\n")[0]
                code_lines = " ".join(code.split("\n")[0:20])

                commit_id = sample["commit_id"]
                commit_root = f"{out_root}/{project}/{commit_id}"
                print(
                    f"Processing: {project}/{commit_id}. Label: {sample['label']}"
                )
                init_output_dirs(out_root, project, commit_id)
                if exists(f"{out_root}/{project}/{commit_id}/label.json"):
                    print(
                        f"Exists: {out_root}/{project}/{commit_id}/label.json")
                    with open(f"{out_root}/{project}/{commit_id}/label.json",
                              "r") as f:
                        labels = json.load(f)

                repo = Repository(join(src_root, project), single=commit_id)
                commit = list(repo.traverse_commits())[0]

                for modi_file in commit.modified_files:
                    if (len(modi_file.methods_before) == 0):
                        continue
                    if (len(modi_file.methods) == 0):
                        continue

                    for method in modi_file.methods:
                        if match_method_to_codelines(method, code_firstline,
                                                     code_lines):
                            safe_method = method
                            file_path = modi_file.new_path
                            file_name = basename(file_path)
                            print(
                                f"find method: {file_path}/{safe_method.long_name}"
                            )
                            write_source_code(file_path, commit_root,
                                              modi_file.source_code)
                            label = {}
                            label["label"] = 0
                            label["project"] = project
                            label["commitid"] = commit_id
                            label["file_name"] = file_name
                            label["file_path"] = file_path

                            label["method"] = [
                                safe_method.name, safe_method.long_name,
                                safe_method.start_line, safe_method.end_line
                            ]
                            label["file_flaws"] = []
                            label["method_flaws"] = []
                            label["flows"] = [[]]
                            label["source"] = "fan"
                            labels.append(label)
                if len(labels) == 0:
                    os.system(f"rm -r {out_root}/{project}/{commit_id}")
                    with open(done_idxes_path, "a") as f:
                        f.write(str(idx) + ",")
                    continue
                ct += 1
                with open(f"{out_root}/{project}/{commit_id}/label.json",
                          "w",
                          encoding="utf-8") as f:
                    json.dump(labels, f, indent=2)
                print(f"Done writing: {commit_root}/label.json")

            else:
                code = sample["func_before"]
                code_firstline = code.split("\n")[0]
                code_lines = " ".join(code.split("\n")[0:20])
                # ct = ct - 1
                commit_id = sample["commit_id"]

                repo = Repository(join(src_root, project), single=commit_id)
                commit = list(repo.traverse_commits())[0]
                before_commit_id = commit.parents[0]
                commit_root = f"{out_root}/{project}/{before_commit_id}"
                print(
                    f"Processing: {project}/{before_commit_id}. Label: {sample['label']}"
                )
                init_output_dirs(out_root, project, before_commit_id)

                if exists(
                        f"{out_root}/{project}/{before_commit_id}/label.json"):
                    print(
                        f"Exists: {out_root}/{project}/{before_commit_id}/label.json"
                    )
                    with open(
                            f"{out_root}/{project}/{before_commit_id}/label.json",
                            "r") as f:
                        labels = json.load(f)
                # if commit_id in before_to_after:
                #     after_commits = before_to_after[commit_id]

                for modi_file in commit.modified_files:
                    if (len(modi_file.methods_before) == 0):
                        continue
                    name_to_method_before, stline_to_method_before = build_name_to_method(
                        modi_file.methods_before)

                    file_path = modi_file.old_path
                    if file_path is not None:
                        file_name = basename(file_path)
                        # contain deleted lines
                        if is_cpp(file_name) and modi_file.deleted_lines > 0:
                            deleted_lines = [
                                i[0] for i in modi_file.diff_parsed["deleted"]
                            ]
                            vul_methods = find_vul_methods(
                                deleted_lines, stline_to_method_before)
                            for vul_method in vul_methods:
                                if match_method_to_codelines(
                                        name_to_method_before[vul_method],
                                        code_firstline, code_lines):
                                    v_method = name_to_method_before[
                                        vul_method]
                                    print(
                                        f"find method: {file_path}/{v_method.long_name}"
                                    )
                                    write_source_code(
                                        file_path, commit_root,
                                        modi_file.source_code_before)
                                    label = {}
                                    label["label"] = 1
                                    label["project"] = project
                                    label["commitid"] = before_commit_id
                                    label["file_name"] = file_name
                                    label["file_path"] = file_path
                                    label["CWEID"] = sample["CWEID"]
                                    label["CVEID"] = sample["CVEID"]
                                    label["source"] = "fan"

                                    label["method"] = [
                                        v_method.name, v_method.long_name,
                                        v_method.start_line, v_method.end_line
                                    ]
                                    label["file_flaws"] = deleted_lines

                                    label["method_flaws"] = [
                                        deleted_line
                                        for deleted_line in deleted_lines if ((
                                            deleted_line >= v_method.start_line
                                        ) and (
                                            deleted_line <= v_method.end_line))
                                    ]

                                    label["flows"] = [[]]
                                    labels.append(label)
                if len(labels) == 0:
                    os.system(f"rm -r {out_root}/{project}/{before_commit_id}")
                    with open(done_idxes_path, "a") as f:
                        f.write(str(idx) + ",")
                    continue
                ct += 1
                with open(
                        f"{out_root}/{project}/{before_commit_id}/label.json",
                        "w",
                        encoding="utf-8") as f:
                    json.dump(labels, f, indent=2)
                print(f"Done writing: {commit_root}/label.json")
        except Exception as e:
            logger.error(e)
            continue
        with open(done_idxes_path, "a") as f:
            f.write(str(idx) + ",")
    print(f"end -- label for Fan. passed {ct}/{len(raw_label)}")




if __name__ == "__main__":

    auto_label_fan()