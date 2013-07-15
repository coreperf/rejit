var kiB = 1024;
var MiB = kiB * kiB;
var GiB = MiB * kiB;

function suffix_format(val, suffix) {
  if (val > GiB)
    return (val / GiB).toFixed(1) + " G" + suffix;
  if (val > MiB)
    return (val / MiB).toFixed(0) + " M" + suffix;
  else if (val > kiB)
    return (val / kiB).toFixed(0) + " k" + suffix;
  else
    return (val / 1.0).toFixed(0) + " " + suffix;
}

function byte_format(val) { return suffix_format(val, "iB"); }
function byte_sec_format(val) { return suffix_format(val, "iB/s"); }

function yFormatter(val, axis) { return byte_sec_format(val); }

var plot_options_parallel = {
  series: { lines: { show: true },
            points: { show: true, radius: 2} },
  legend: { position: "nw" },
  grid:   { hoverable: true, clickable: true },
  xaxis:  { ticks: [[8,"8 B"], [16,"16 B"], [64,"64 B"], [256,"256 B"], [kiB,"1 kiB"], [4*kiB,"4 kiB"], [16*kiB,"16 kiB"], [64*kiB,"64 kiB"], [256*kiB,"256 kiB"], [1*MiB,"1 MiB"], [4*MiB,"4 MiB"], [16*MiB,"16 MiB"]],
            transform: function (v) { return Math.log(v); },
            inverseTransform: function (v) { return Math.exp(v); } },
  yaxis:  { tickFormatter: yFormatter }
};

var plot_options_speed_time = {
  series: { lines: { show: true },
            points: { show: true, radius: 2} },
  legend: { position: "nw" },
  grid:   { hoverable: true, clickable: true },
  xaxis:  { mode: "time" },
  yaxis:  { tickFormatter: yFormatter }
};


function show_tooltip(x, y, color, contents) {
  $('<div id="tooltip">' + contents + '</div>').css( {
    position: 'absolute',
    display: 'none',
    top: y + 5,
    left: x + 5,
    border: '1px solid #fdd',
    padding: '2px',
    'background-color': color,
    opacity: 0.80
  }).appendTo("body").fadeIn(200);
}


function plothover_func(event, pos, item) {
  $("#x").text(pos.x.toFixed(2));
  $("#y").text(pos.y.toFixed(2));

  if (item) {
    if (previousPoint != item.dataIndex) {
      previousPoint = item.dataIndex;

      $("#tooltip").remove();
      var x = item.datapoint[0].toFixed(2),
          y = item.datapoint[1].toFixed(2);

      show_tooltip(item.pageX, item.pageY,
                  item.series.color,
                  item.series.label + '<br/>' +
                  byte_sec_format(y) + ' (' + Math.floor(y) + ' B/s)' + '<br/>' +
                  byte_format(x)  + ' (' + Math.floor(x) + ' B)');
    }
  }
  else {
    $("#tooltip").remove();
    previousPoint = null;
  }
}

function plot_according_to_choices(graph_id, data_sets, choice_container) {
  var data = [];
  choice_container.find("input:checked").each(function () {
    var key = $(this).attr("name");
    if (key && data_sets[key])
    data.push(data_sets[key]);
  });
  if (data.length > 0)
    $.plot($("#" + graph_id), data, plot_options_parallel);
}
