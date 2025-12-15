import os
import shlex

from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, ExecuteProcess, OpaqueFunction, SetEnvironmentVariable
from launch.substitutions import LaunchConfiguration


def _make_cmd(context):
    # script location inside the container (adjust if yours differs)
    script = LaunchConfiguration("script").perform(context)

    network = LaunchConfiguration("network").perform(context)
    threshold = LaunchConfiguration("threshold").perform(context)

    headless = LaunchConfiguration("headless").perform(context).lower() in ("1", "true", "yes", "on")
    overlay = LaunchConfiguration("overlay").perform(context)

    input_width = LaunchConfiguration("input_width").perform(context)
    input_height = LaunchConfiguration("input_height").perform(context)

    ssl_cert = LaunchConfiguration("ssl_cert").perform(context)
    ssl_key = LaunchConfiguration("ssl_key").perform(context)

    input_uri = LaunchConfiguration("input_uri").perform(context)
    output_uri = LaunchConfiguration("output_uri").perform(context)

    # Optional: any extra jetson-utils args you want forwarded verbatim (string)
    # Example: extra_args:="--input-rate=30 --codec=h264"
    extra_args = LaunchConfiguration("extra_args").perform(context)
    extra_list = shlex.split(extra_args) if extra_args else []

    cmd = ["python3", script]
    cmd += [f"--network={network}"]
    cmd += [f"--threshold={threshold}"]
    cmd += [f"--overlay={overlay}"]
    cmd += [f"--input-width={input_width}", f"--input-height={input_height}"]

    if headless:
        cmd += ["--headless"]

    # These are used by WebRTC / your script
    if ssl_cert:
        cmd += [f"--ssl-cert={ssl_cert}"]
    if ssl_key:
        cmd += [f"--ssl-key={ssl_key}"]

    cmd += extra_list

    # Match your working CLI: positional input/output URIs at the end
    cmd += [input_uri, output_uri]

    return cmd


def generate_launch_description():
    # Defaults assume you're running inside the jetson-inference container,
    # where the repo is typically at /jetson-inference
    default_script = "/jetson-inference/python/www/dash/inference_server.py"

    return LaunchDescription([
        DeclareLaunchArgument("script", default_value=default_script),
        DeclareLaunchArgument("network", default_value="facenet"),
        DeclareLaunchArgument("threshold", default_value="0.8"),
        DeclareLaunchArgument("headless", default_value="true"),
        DeclareLaunchArgument("overlay", default_value="none"),
        DeclareLaunchArgument("input_width", default_value="360"),
        DeclareLaunchArgument("input_height", default_value="240"),
        DeclareLaunchArgument("input_uri", default_value="webrtc://@:8554/input"),
        DeclareLaunchArgument("output_uri", default_value="webrtc://@:8554/output"),
        DeclareLaunchArgument("ssl_cert", default_value="/jetson-inference/data/cert.pem"),
        DeclareLaunchArgument("ssl_key", default_value="/jetson-inference/data/key.pem"),
        DeclareLaunchArgument("extra_args", default_value=""),

        # Optional: keep these in the environment too (your Docker entrypoint already tries)
        SetEnvironmentVariable(name="SSL_CERT", value=LaunchConfiguration("ssl_cert")),
        SetEnvironmentVariable(name="SSL_KEY", value=LaunchConfiguration("ssl_key")),

        OpaqueFunction(function=lambda context: [
            ExecuteProcess(
                cmd=_make_cmd(context),
                output="screen",
                respawn=True,          # similar behavior to respawn="true"
                respawn_delay=2.0,
            )
        ]),
    ])