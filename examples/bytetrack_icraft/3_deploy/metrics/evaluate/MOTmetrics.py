import os
import sys
import time
import argparse
from tqdm import tqdm
import motmetrics as mm
from pathlib import Path
from loguru import logger

class Logger(object):
    def __init__(self, filename="Default.log", path="./"):
        self.terminal = sys.stdout
        self.log = open(os.path.join(path, filename), "a", encoding='utf8')

    def write(self, message):
        self.terminal.write(message)
        self.log.write(message)
        self.log.flush()

    def flush(self):
        pass

def getLog(log_root, fnsfx):
    log_root=os.path.abspath(log_root)
    if not os.path.exists(log_root):
        print(f'\033[31;43mwarning: log path is not exist: create new path <{log_root}> \033[0m')
        os.mkdir(log_root)
    log_time = time.strftime("%Y-%m-%d-%H_%M_%S",time.localtime(time.time()))
    sys.stdout = Logger(f"{log_root}/{log_time}_{fnsfx}.log")

# get videos for test from a txt file (same as runtimeapp)
def get_videos_names(video_list_txt_path):
    with open(video_list_txt_path, 'r') as f:
        video_names = f.read().splitlines()
    # video_names = list(map(lambda x: os.path.splitext(x)[0], video_names))
    return video_names

def compare_dataframes(gts, ts):
    accs = []
    names = []
    for k, tsacc in ts.items():

        if k in gts:            
            logger.info('Comparing {}...'.format(k))
            accs.append(mm.utils.compare_to_groundtruth(gts[k], tsacc, 'iou', distth=0.5))
            names.append(k)
        else:
            logger.warning('No ground truth for {}, skipping.'.format(k))

    return accs, names

def parse_path():
    parser = argparse.ArgumentParser()
    parser.add_argument('result_txt', help='onboard video txt results path')
    paths = parser.parse_args()
    return paths

if __name__ == '__main__':
    # test
    # --result_txt "E:\GitLab\metrics\compile_workspace\results_onboard\bytetrack_16_pc_0613" 

    train_root = "./labels"
    # video_names_txt_path = "./MOT17-half-list.txt"
    video_names_txt_path = "./MOT17-val_list.txt"
    train_list = os.listdir(train_root)

    paths = parse_path()
    # we only calculate metrics of videos which we test on icraft
    video_names_test = get_videos_names(video_names_txt_path)
    gtfile_tsfile_map = {}
    videol = []
    for video_name in video_names_test:
        # fetch gt txt file from train folder
        gtfile = os.path.join(train_root, video_name, "gt.txt")
        tsfile = os.path.join(paths.result_txt, video_name + ".txt")
        if os.path.exists(tsfile):
            gtfile_tsfile_map[gtfile] = tsfile
            videol.append(video_name)

    # acc = mm.MOTAccumulator()
    mm.lap.default_solver = 'lap'

    log_root='./classlog'
    getLog(log_root, Path(paths.result_txt).name)

    fullaccl = []
    partaccl = []
    for gtfile, tsfile in tqdm(gtfile_tsfile_map.items()):
        # print("============================")

        gt = mm.io.loadtxt(fname=gtfile, fmt='mot15-2D', min_confidence=1)
        ts = mm.io.loadtxt(fname=tsfile, fmt='mot15-2D', min_confidence=-1)
        acc = mm.utils.compare_to_groundtruth(gt, ts, 'iou', distth=0.5)
        mh = mm.metrics.create()

        fullaccl.append(acc)
        partaccl.append(acc.events.loc[0:3])

    for view, accl in zip(["FULL", "PART"], [fullaccl, partaccl]):
        print(f"## {view}")
        summary = mh.compute_many(accl,
                                  metrics=mm.metrics.motchallenge_metrics,
                                  names=videol,
                                  generate_overall=True)
        strsummary = mm.io.render_summary(summary,
                                          formatters=mh.formatters,
                                          namemap=mm.io.motchallenge_metric_names)
        print(strsummary)
        print('\n')

