$DATASET_PATH = "../../../dataset/plosha_dataset.csv"

Write-Host "-- baseline: Fault-Tolerant Workflow (Ref[37]) --"
cd e:\PLOSHA\schemes\fault_tolerant_workflow\src
New-Item -ItemType Directory -Force -Path ..\exp2_scheduling_efficiency
.\ftworkflow.exe --experiment 9 --dataset $DATASET_PATH

Write-Host "-- baseline: FedDQN (Ref[22]) --"
cd e:\PLOSHA\schemes\fed_dqn\src
New-Item -ItemType Directory -Force -Path ..\exp2_scheduling_efficiency
.\exp9_scheduling_efficiency.exe

Write-Host "-- baseline: FT-Serverless Edge (Ref[38]) --"
cd e:\PLOSHA\schemes\ft_serverless_edge\src
New-Item -ItemType Directory -Force -Path ..\exp2_scheduling_efficiency
"num_fog_nodes,scheduling_latency_ms,workload_imbalance" | Out-File -FilePath ..\exp2_scheduling_efficiency\results.csv -Encoding ASCII
$BASE_SEED = 12345
foreach ($v in 5,10,15,20,25,30,35,40,45,50) {
    $sensors = $v * 100
    $line = .\ft_serverless_sim.exe --experiment 9 --variable $v --cloudlets $v --sensors $sensors --seed $BASE_SEED --dataset $DATASET_PATH --heterogeneity 5.0
    $line | Out-File -FilePath ..\exp2_scheduling_efficiency\results.csv -Append -Encoding ASCII
}

Write-Host "-- baseline: PLOSHA-RMFR (Ours) --"
cd e:\PLOSHA\schemes\plosha_rmfr\src
New-Item -ItemType Directory -Force -Path ..\exp2_scheduling_efficiency_native
.\plosha_rmfr.exe --experiment 9
