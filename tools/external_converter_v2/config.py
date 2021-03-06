#! /usr/bin/env python
# Copyright (c) 2017, Cuichaowen. All rights reserved.
# -*- coding: utf-8 -*-

import argparse
import enum
import os
import sys
import subprocess
from yaml import load, dump
try:
    from yaml import CLoader as Loader, CDumper as Dumper
except ImportError:
    from yaml import Loader, Dumper

ConfigFilePath = './config.yaml'


class Configuration:
    """
    Parse the config.yaml file.
    Configuration holds all the params defined in configfile.
    """
    def __init__(self, args, config_file_path=ConfigFilePath):
        data = load(open(config_file_path, 'r').read())
        self.fill_config_from_args(args, data)

        # parse Options from config file.
        self.DebugConfig = data['DEBUG'] if 'DEBUG' in data else None
        self.framework = data['OPTIONS']['Framework']
        self.SavePath = data['OPTIONS']['SavePath'] \
                if data['OPTIONS']['SavePath'][-1] == '/' \
                else data['OPTIONS']['SavePath'] + '/'
        self.ResultName = data['OPTIONS']['ResultName']
        #self.intermediateModelPath = data['OPTIONS']['SavePath'] \
        #        + data['OPTIONS']['ResultName'] + "anakin.bin"
        self.LaunchBoard = True if data['OPTIONS']['Config']['LaunchBoard'] else False
        self.ip = data['OPTIONS']['Config']['Server']['ip']
        self.port = data['OPTIONS']['Config']['Server']['port']
        self.hasOptimizedGraph = True if data['OPTIONS']['Config']['OptimizedGraph']['enable'] else False
        self.optimizedGraphPath = data['OPTIONS']['Config']['OptimizedGraph']['path'] \
                if self.hasOptimizedGraph else ""
        self.logger_dict = data['OPTIONS']['LOGGER']
        self.framework_config_dict = data['TARGET'][self.framework]
        self.check_protobuf_version()
        if 'ProtoPaths' in data['TARGET'][self.framework].keys():
            proto_list = data['TARGET'][self.framework]['ProtoPaths']
            self.__refresh_pbs(proto_list)
        self.generate_pbs_of_anakin()

    def fill_config_from_args(self, args, data):
        """Fill config from args
        """
        # set common args
        if args.debug is not None:
            data['DEBUG'] = args.debug
        if args.framework is not None:
            data['OPTIONS']['Framework'] = str(args.framework)
        if args.save_path is not None:
            data['OPTIONS']['SavePath'] = args.save_path
        if args.result_name is not None:
            data['OPTIONS']['ResultName'] = args.result_name
        if args.open_launch_board is not None:
            data['OPTIONS']['Config']['LaunchBoard'] = True if args.open_launch_board != 0 else False
        if args.board_server_ip is not None:
            data['OPTIONS']['Config']['Server']['ip'] = args.board_server_ip
        if args.board_server_port is not None:
            data['OPTIONS']['Config']['Server']['port'] = args.board_server_port
        if args.optimized_graph_enable is not None:
            data['OPTIONS']['Config']['OptimizedGraph']['enable'] = True if args.optimized_graph_enable != 0 else False
        if args.optimized_graph_path is not None:
            data['OPTIONS']['Config']['OptimizedGraph']['path'] = args.optimized_graph_path
        if args.log_path is not None:
            data['OPTIONS']['LOGGER']['LogToPath'] = args.log_path
        if args.log_with_color is not None:
            data['OPTIONS']['LOGGER']['WithColor'] = args.log_with_color

        # set framwork specific args
        # caffe
        if args.caffe_proto_paths is not None:
            data['TARGET']['CAFFE']['ProtoPaths'] = args.caffe_proto_paths
        if args.caffe_proto_txt_path is not None:
            data['TARGET']['CAFFE']['PrototxtPath'] = args.caffe_proto_txt_path
        if args.caffe_model_path is not None:
            data['TARGET']['CAFFE']['ModelPath'] = args.caffe_model_path
        if args.caffe_remark is not None:
            data['TARGET']['CAFFE']['Remark'] = args.caffe_remark

        # fluid
        if args.fluid_debug is not None:
            data['TARGET']['FLUID']['Debug'] = args.fluid_debug
        if args.fluid_model_path is not None:
            data['TARGET']['FLUID']['ModelPath'] = args.fluid_model_path
        if args.fluid_net_type is not None:
            data['TARGET']['FLUID']['NetType'] = args.fluid_net_type

        # lego
        if args.lego_proto_path is not None:
            data['TARGET']['LEGO']['ProtoPath'] = args.lego_proto_path
        if args.lego_prototxt_path is not None:
            data['TARGET']['LEGO']['PrototxtPath'] = args.lego_prototxt_path
        if args.lego_model_path is not None:
            data['TARGET']['LEGO']['ModelPath'] = args.lego_model_path

        # tensorflow
        if args.tensorflow_model_path is not None:
            data['TARGET']['TENSORFLOW']['ModelPath'] = args.tensorflow_model_path
        if args.tensorflow_outputs is not None:
            data['TARGET']['TENSORFLOW']['OutPuts'] = args.tensorflow_outputs

        # onnx
        if args.onnx_model_path is not None:
            data['TARGET']['ONNX']['ModelPath'] = args.onnx_model_path

        # houyi
        if args.houyi_model_path is not None:
            data['TARGET']['HOUYI']['ModelPath'] = args.houyi_model_path
        if args.houyi_weights_path is not None:
            data['TARGET']['HOUYI']['WeightsPath'] = args.houyi_weights_path

    def check_protobuf_version(self):
        """
        Check if the pip-protoc version is equal to sys-protoc version.
        """
        assert sys.version_info[0] == 2
        for path in sys.path:
            module_path = os.path.join(path, 'google', 'protobuf', '__init__.py')
            if os.path.exists(module_path):
                with open(module_path) as f:
                    __name__ = '__main__'
                    exec(f.read(), locals())
                break
        try:
            protoc_out = subprocess.check_output(["protoc", "--version"]).split()[1]
        except OSError as exc:
            raise OSError('Can not find Protobuf in system environment.')
        try:
            __version__
        except NameError:
            raise OSError('Can not find Protobuf in python environment.')
        sys_versions = map(int, protoc_out.split('.'))
        pip_versions = map(int, __version__.split('.'))
        assert sys_versions[0] >= 3 and pip_versions[0] >= 3, \
            "Protobuf version must be greater than 3.0. Please refer to the Anakin Docs."
        assert pip_versions[1] >= sys_versions[1], \
            "ERROR: Protobuf must be the same.\nSystem Protobuf %s\nPython Protobuf %s\n" \
            % (protoc_out, __version__) + "Try to execute pip install protobuf==%s" % (protoc_out)

    def generate_pbs_of_anakin(self):
        protoFilesStr = subprocess.check_output(["ls", "parser/proto/"])
        filesList = protoFilesStr.split('\n')
        protoFilesList = []
        for file in filesList:
            suffix = file.split('.')[-1]
            if suffix == "proto":
                protoFilesList.append("parser/proto/" + file)
        if len(protoFilesList) == 0:
            raise NameError('ERROR: There does not have any proto files in proto/ ')
        self.__refresh_pbs(protoFilesList, "parser/proto/")

    def pbs_eraser(self, folder_path):
        """
        Delete existing pb2 to avoid conflicts.
        """
        for file_name in os.listdir(os.path.dirname(folder_path)):
            if "pb2." in file_name:
                file_path = os.path.join(folder_path, file_name)
                os.remove(file_path)

    def __refresh_pbs(self, proto_list, default_save_path="parser/pbs/"):
        """
        Genetrate pb files according to proto file list and saved to parser/pbs/ finally.
        parameter:
                proto_list: ['/path/to/proto_0','/path/to/proto_1', ... ]
                default_save_path: default saved to 'parser/pbs/'
        """
        self.pbs_eraser(default_save_path)
        assert type(proto_list) == list, \
        "The ProtoPaths format maybe incorrect, please check if there is any HORIZONTAL LINE."
        for pFile in proto_list:
            assert os.path.exists(pFile), "%s does not exist.\n" % (pFile)
            subprocess.check_call(['protoc', '-I',
                                   os.path.dirname(pFile) + "/",
                                   '--python_out', os.path.dirname(default_save_path) + "/", pFile])

