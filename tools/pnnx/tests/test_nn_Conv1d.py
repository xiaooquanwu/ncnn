# Tencent is pleased to support the open source community by making ncnn available.
#
# Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
#
# Licensed under the BSD 3-Clause License (the "License"); you may not use this file except
# in compliance with the License. You may obtain a copy of the License at
#
# https://opensource.org/licenses/BSD-3-Clause
#
# Unless required by applicable law or agreed to in writing, software distributed
# under the License is distributed on an "AS IS" BASIS, WITHOUT WARRANTIES OR
# CONDITIONS OF ANY KIND, either express or implied. See the License for the
# specific language governing permissions and limitations under the License.

import torch
import torch.nn as nn
import torch.nn.functional as F

class Model(nn.Module):
    def __init__(self):
        super(Model, self).__init__()

        self.conv_0 = nn.Conv1d(in_channels=12, out_channels=16, kernel_size=3)
        self.conv_1 = nn.Conv1d(in_channels=16, out_channels=20, kernel_size=2, stride=2, padding=2, dilation=1)
        self.conv_2 = nn.Conv1d(in_channels=20, out_channels=24, kernel_size=3, stride=1, padding=(4), dilation=1, groups=1, bias=False)
        self.conv_3 = nn.Conv1d(in_channels=24, out_channels=28, kernel_size=5, stride=1, padding='valid', dilation=1, groups=4, bias=True)
        self.conv_4 = nn.Conv1d(in_channels=28, out_channels=32, kernel_size=3, stride=1, padding='same', dilation=2, groups=2, bias=False, padding_mode='zeros')
        self.conv_5 = nn.Conv1d(in_channels=32, out_channels=32, kernel_size=2, stride=2, padding=3, dilation=1, groups=32, bias=True, padding_mode='reflect')
        self.conv_6 = nn.Conv1d(in_channels=32, out_channels=28, kernel_size=2, stride=1, padding=2, dilation=1, groups=1, bias=False, padding_mode='replicate')
        #self.conv_7 = nn.Conv1d(in_channels=28, out_channels=24, kernel_size=3, stride=2, padding=(5,6), dilation=2, groups=1, bias=True, padding_mode='circular')

    def forward(self, x):
        x = self.conv_0(x)
        x = self.conv_1(x)
        x = self.conv_2(x)
        x = self.conv_3(x)
        x = self.conv_4(x)
        x = self.conv_5(x)
        x = self.conv_6(x)
        #x = self.conv_7(x)

        return x

def test():
    net = Model()
    net.eval()

    torch.manual_seed(0)
    x = torch.rand(1, 12, 64)

    a = net(x)

    # export torchscript
    mod = torch.jit.trace(net, x)
    mod.save("test_nn_Conv1d.pt")

    # torchscript to pnnx
    import os
    os.system("../src/pnnx test_nn_Conv1d.pt inputshape=[1,12,64]")

    # pnnx inference
    import test_nn_Conv1d_pnnx
    b = test_nn_Conv1d_pnnx.test_inference()

    return torch.equal(a, b)

if __name__ == "__main__":
    if test():
        exit(0)
    else:
        exit(1)
