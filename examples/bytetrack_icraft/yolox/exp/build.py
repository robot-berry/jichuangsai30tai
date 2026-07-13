#!/usr/bin/env python3
# -*- coding:utf-8 -*-
# Copyright (c) 2014-2021 Megvii Inc. All rights reserved.

import importlib
import os
import sys


def get_exp_by_file(exp_file):
  try:
    exp_file = os.path.abspath(exp_file)
    exp_dir = os.path.dirname(exp_file)
        
        # 添加必要的路径
    project_root = os.path.dirname(os.path.dirname(exp_dir))  # 假设项目根目录在exps的上两级
    sys.path.insert(0, project_root)  # 添加项目根目录
    sys.path.insert(0, exp_dir)  # 添加文件所在目录
        
        # 获取模块名（不含扩展名）
    module_name = os.path.basename(exp_file).split(".")[0]
        
        # 动态导入模块
    current_exp = importlib.import_module(module_name)

    
        # 获取Exp类
    if hasattr(current_exp, "Exp"):
        exp_class = current_exp.Exp
    elif hasattr(current_exp, "exp"):
        exp_class = current_exp.exp  # 处理大小写不一致
    else:
        raise AttributeError("No Exp class found")
        
    return exp_class()
  except ImportError as e:
    raise ImportError(f"Failed to import module from {exp_file}: {str(e)}")
  except AttributeError as e:
    raise ImportError(f"Module {module_name} doesn't contain Exp class: {str(e)}")
  except Exception as e:
    raise ImportError(f"Unexpected error loading {exp_file}: {str(e)}")

   


def get_exp_by_name(exp_name):
    import yolox

    yolox_path = os.path.dirname(os.path.dirname(yolox.__file__))
    filedict = {
        "yolox-s": "yolox_s.py",
        "yolox-m": "yolox_m.py",
        "yolox-l": "yolox_l.py",
        "yolox-x": "yolox_x.py",
        "yolox-tiny": "yolox_tiny.py",
        "yolox-nano": "nano.py",
        "yolov3": "yolov3.py",
    }
    filename = filedict[exp_name]
    exp_path = os.path.join(yolox_path, "exps", "default", filename)
    return get_exp_by_file(exp_path)


def get_exp(exp_file, exp_name):
    """
    get Exp object by file or name. If exp_file and exp_name
    are both provided, get Exp by exp_file.

    Args:
        exp_file (str): file path of experiment.
        exp_name (str): name of experiment. "yolo-s",
    """
    assert (
        exp_file is not None or exp_name is not None
    ), "plz provide exp file or exp name."
    if exp_file is not None:
        return get_exp_by_file(exp_file)
    else:
        return get_exp_by_name(exp_name)
